//
// C++ Implementation: phylotree
//
// Description:
//
//
// Author: BUI Quang Minh, Steffen Klaere, Arndt von Haeseler <minh.bui@univie.ac.at>, (C) 2008
//
// Copyright: See COPYING file that comes with this distribution
//
//

#include "phylotree.h"
#include "bionj.h"
//#include "rateheterogeneity.h"
#include "alignmentpairwise.h"
#include <algorithm>
#include <limits>
#include "timeutil.h"

//const static int BINARY_SCALE = floor(log2(1/SCALING_THRESHOLD));
//const static double LOG_BINARY_SCALE = -(log(2) * BINARY_SCALE);

/****************************************************************************
        SPRMoves class
 ****************************************************************************/

void SPRMoves::add(PhyloNode *prune_node, PhyloNode *prune_dad,
                   PhyloNode *regraft_node, PhyloNode *regraft_dad, double score) {
    if (size() >= MAX_SPR_MOVES && score <= rbegin()->score)
        return;
    if (size() >= MAX_SPR_MOVES) {
        iterator it = end();
        it--;
        erase(it);
    }
    SPRMove spr;
    spr.prune_node = prune_node;
    spr.prune_dad = prune_dad;
    spr.regraft_node = regraft_node;
    spr.regraft_dad = regraft_dad;
    spr.score = score;
    insert(spr);
}

/****************************************************************************
        PhyloTree class
 ****************************************************************************/

PhyloTree::PhyloTree()
        : MTree() {
	init();
}

void PhyloTree::init() {
    aln = NULL;
    model = NULL;
    site_rate = NULL;
    optimize_by_newton = false;
    central_partial_lh = NULL;
    central_scale_num = NULL;
    central_partial_pars = NULL;
    model_factory = NULL;
    tmp_partial_lh1 = NULL;
    tmp_partial_lh2 = NULL;
    tmp_scale_num1 = NULL;
    tmp_scale_num2 = NULL;
    discard_saturated_site = true;
    _pattern_lh = NULL;

}

PhyloTree::PhyloTree(Alignment *aln) : MTree() {
	init();
    this->aln = aln;
    alnSize = aln->size();
    numStates = aln->num_states;
    tranSize = numStates * numStates;
    ptn_freqs.resize(alnSize);
    for (int ptn = 0; ptn < alnSize; ++ptn) {
        ptn_freqs[ptn] = (*aln)[ptn].frequency;
    }
}

void PhyloTree::discardSaturatedSite(bool val) {
    discard_saturated_site = val;
}

PhyloTree::~PhyloTree() {
    if (central_partial_lh)
        delete [] central_partial_lh;
    central_partial_lh = NULL;
    if (central_scale_num)
        delete [] central_scale_num;
    central_scale_num = NULL;

    if (central_partial_pars)
        delete [] central_partial_pars;
    central_partial_pars = NULL;
    if (model_factory) delete model_factory;
    if (model) delete model;
    if (site_rate) delete site_rate;
    if (tmp_scale_num1)
        delete [] tmp_scale_num1;
    if (tmp_scale_num2)
        delete [] tmp_scale_num2;
    if (tmp_partial_lh1)
        delete [] tmp_partial_lh1;
    if (tmp_partial_lh2)
        delete [] tmp_partial_lh2;
    if (_pattern_lh) delete [] _pattern_lh;
}

void PhyloTree::assignLeafNames(Node *node, Node *dad) {
    if (!node) node = root;
    if (node->isLeaf()) {
        node->id = atoi(node->name.c_str());
        assert(node->id >= 0 && node->id < leafNum);
        node->name = aln->getSeqName(node->id).c_str();
    }
    FOR_NEIGHBOR_IT(node, dad, it)
    assignLeafNames((*it)->node, node);
}

void PhyloTree::copyTree(MTree *tree) {
    MTree::copyTree(tree);
    if (!aln) return;
    // reset the ID with alignment
    setAlignment(aln);
}

void PhyloTree::copyTree(MTree *tree, string &taxa_set) {
    MTree::copyTree(tree, taxa_set);
    if (!aln) return;
    // reset the ID with alignment
    setAlignment(aln);
}

void PhyloTree::copyPhyloTree(PhyloTree *tree) {
    MTree::copyTree(tree);
    if (!tree->aln) return;
    setAlignment(tree->aln);
}

void PhyloTree::setAlignment(Alignment *alignment) {
    aln = alignment;
    alnSize = aln->size();
    ptn_freqs.resize(alnSize);
    numStates = aln->num_states;
    tranSize = numStates * numStates;
    ptn_freqs.resize(alnSize);
    for (int ptn = 0; ptn < alnSize; ++ptn) {
        ptn_freqs[ptn] = (*aln)[ptn].frequency;
    }
    block = aln->num_states * numCat;
    lh_size = aln->size() * block;

    int nseq = aln->getNSeq();
    for (int seq = 0; seq < nseq; seq++) {
        string seq_name = aln->getSeqName(seq);
        Node *node = findLeafName(seq_name);
        if (!node) {
            string str = "Alignment has a sequence name ";
            str += seq_name;
            str += " which is not in the tree";
            outError(str);
        }
        assert(node->isLeaf());
        node->id = seq;
    }
}

void PhyloTree::rollBack(istream &best_tree_string) {
    best_tree_string.seekg(0, ios::beg);
    freeNode();
    readTree(best_tree_string, rooted);
    assignLeafNames();
    initializeAllPartialLh();
}

void PhyloTree::setModel(SubstModel *amodel) {
    model = amodel;
}

void PhyloTree::setModelFactory(ModelFactory *model_fac) {
    model_factory = model_fac;
}

void PhyloTree::setRate(RateHeterogeneity *rate) {
    site_rate = rate;
    if (!rate) return;
    numCat = site_rate->getNRate();
    if (aln) {
        block = aln->num_states * numCat;
        lh_size = aln->size() * block;
    }
}

RateHeterogeneity *PhyloTree::getRate() {
    return site_rate;
}

Node* PhyloTree::newNode(int node_id, const char* node_name) {
    return (Node*) (new PhyloNode(node_id, node_name));
}

Node* PhyloTree::newNode(int node_id, int node_name) {
    return (Node*) (new PhyloNode(node_id, node_name));
}

void PhyloTree::clearAllPartialLH() {
    if (!root) return;
    ((PhyloNode*) root->neighbors[0]->node)->clearAllPartialLh((PhyloNode*) root);
}

string PhyloTree::getModelName() {
    return model->name + site_rate->name;
}

/****************************************************************************
        Parsimony function
 ****************************************************************************/



int PhyloTree::getBitsBlockSize() {
    // reserve the last entry for parsimony score
    return (aln->num_states * aln->size() + UINT_BITS - 1) / UINT_BITS + 1;
}

int PhyloTree::getBitsEntrySize() {
    // reserve the last entry for parsimony score
    return (aln->num_states + UINT_BITS - 1) / UINT_BITS;
}

UINT *PhyloTree::newBitsBlock() {
    return new UINT[getBitsBlockSize()];
}

void PhyloTree::getBitsBlock(UINT *bit_vec, int index, UINT* &bits_entry) {
    int nstates = aln->num_states;
    int myindex = (index * nstates);
    int bit_pos_begin = myindex >> BITS_DIV;
    int bit_off_begin = myindex & BITS_MODULO;
    int bit_pos_end = (myindex + nstates) >> BITS_DIV;
    int bit_off_end = (myindex + nstates) & BITS_MODULO;

    if (bit_pos_begin == bit_pos_end) {
        bits_entry[0] = (bit_vec[bit_pos_begin] >> bit_off_begin) & ((1 << nstates) - 1);
        return;
    }
    UINT part1 = (bit_vec[bit_pos_begin] >> bit_off_begin);
    int rest_bits = nstates;
    int id;
    for (id = 0; rest_bits >= UINT_BITS; id++, rest_bits -= UINT_BITS, bit_pos_begin++) {
        bits_entry[id] = part1;
        if (bit_off_begin > 0) bits_entry[id] |= (bit_vec[bit_pos_begin+1] << (UINT_BITS - bit_off_begin));
        part1 = (bit_vec[bit_pos_begin+1] >> bit_off_begin);
    }
    if (bit_pos_begin == bit_pos_end) {
        bits_entry[id] = (bit_vec[bit_pos_begin] >> bit_off_begin) & ((1 << rest_bits) - 1);
        return;
    }
    UINT part2 = bit_vec[bit_pos_end];
    if (bit_off_end < UINT_BITS) part2 &= ((1 << bit_off_end) - 1);
    bits_entry[id] = part1;
    if (bit_off_begin > 0) bits_entry[id] |= (part2 << (UINT_BITS - bit_off_begin));
}

void PhyloTree::setBitsBlock(UINT* &bit_vec, int index, UINT *bits_entry) {
    int nstates = aln->num_states;
    int myindex = (index * nstates);
    int bit_pos_begin = myindex >> BITS_DIV;
    int bit_off_begin = myindex & BITS_MODULO;
    int bit_pos_end = (myindex + nstates) >> BITS_DIV;
    int bit_off_end = (myindex + nstates) & BITS_MODULO;

    //assert(value <= allstates);

    if (bit_pos_begin == bit_pos_end) {
        // first clear the bit between bit_off_begin and bit_off_end
        int allstates = (1 << nstates) - 1;
        bit_vec[bit_pos_begin] &= ~(allstates << bit_off_begin);
        // now set the bit
        bit_vec[bit_pos_begin] |= bits_entry[0] << bit_off_begin;
        return;
    }
    int len1 = UINT_BITS - bit_off_begin;
    // clear bit from bit_off_begin to UINT_BITS
    bit_vec[bit_pos_begin] &= (1 << bit_off_begin) - 1;
    // set bit  from bit_off_begin to UINT_BITS
    bit_vec[bit_pos_begin] |= (bits_entry[0] << bit_off_begin);
    int rest_bits = nstates - len1;
    int id;
    for (id = 0; rest_bits >= UINT_BITS; bit_pos_begin++, id++, rest_bits -= UINT_BITS) {
        bit_vec[bit_pos_begin+1] = (bits_entry[id+1] << bit_off_begin);
        if (len1 < UINT_BITS) bit_vec[bit_pos_begin+1] |= (bits_entry[id] >> len1);
    }

    assert(bit_pos_begin == bit_pos_end-1);
    // clear bit from 0 to bit_off_end
    bit_vec[bit_pos_end] &= ~((1 << bit_off_end) - 1);
    // now set the bit the value
    if (len1 < UINT_BITS) bit_vec[bit_pos_end] |= (bits_entry[id] >> len1);
    rest_bits -= bit_off_begin;
    if  (rest_bits > 0)
        bit_vec[bit_pos_end] |= (bits_entry[id+1] << bit_off_begin);
}

bool PhyloTree::isEmptyBitsEntry(UINT *bits_entry) {
    int rest_bits = aln->num_states;
    int i;
    for (i = 0; rest_bits >= UINT_BITS; rest_bits -= UINT_BITS, i++)
        if (bits_entry[i]) return false;
    if (bits_entry[i] & ((1 << rest_bits)-1)) return false;
    return true;
}

void PhyloTree::unionBitsEntry(UINT *bits_entry1, UINT *bits_entry2, UINT* &bits_union) {
    int rest_bits = aln->num_states;
    int i;
    for (i = 0; rest_bits > 0; rest_bits -= UINT_BITS, i++)
        bits_union[i] = bits_entry1[i] | bits_entry2[i];
}

void PhyloTree::setBitsEntry(UINT* &bits_entry, int id) {
    int bit_pos = id >> BITS_DIV;
    int bit_off = id & BITS_MODULO;
    bits_entry[bit_pos] |= (1 << bit_off);
}


bool PhyloTree::getBitsEntry(UINT* &bits_entry, int id) {
    int bit_pos = id >> BITS_DIV;
    int bit_off = id & BITS_MODULO;
    if (bits_entry[bit_pos] & (1 << bit_off)) return true;
    return false;
}


void setBitsAll(UINT* &bit_vec, int num) {
    //int id;
    int size = num/UINT_BITS;
    memset(bit_vec, 255, size * sizeof(UINT));
    num &= BITS_MODULO;
    if (num) bit_vec[size] = (1 << num) - 1;
}

void PhyloTree::computePartialParsimony(PhyloNeighbor *dad_branch, PhyloNode *dad) {
    // don't recompute the likelihood
    if (dad_branch->partial_lh_computed & 2)
        return;
    Node *node = dad_branch->node;
    assert(node->degree() <= 3);
    int ptn;
    int nstates = aln->num_states;
    int pars_size = getBitsBlockSize();
    int entry_size = getBitsEntrySize();
    assert(dad_branch->partial_pars);
    UINT *bits_entry = new UINT[entry_size];
    UINT *bits_entry1 = new UINT[entry_size];
    UINT *bits_entry2 = new UINT[entry_size];

    if (node->isLeaf() && dad) {
        // external node
        setBitsAll(dad_branch->partial_pars, nstates * aln->size());
        dad_branch->partial_pars[pars_size-1] = 0;
        for (ptn = 0; ptn < aln->size(); ptn++) {
            char state;
            if (node->name == ROOT_NAME) {
                state = STATE_UNKNOWN;
            } else {
                assert(node->id < aln->getNSeq());
                state = (aln->at(ptn))[node->id];
            }
            if (state == STATE_UNKNOWN) {
                // fill all entries with bit 1
                //setBitsBlock(dad_branch->partial_pars, ptn, (1 << nstates) - 1);
            } else if (state < nstates) {
                memset(bits_entry, 0, sizeof(UINT) * entry_size);
                setBitsEntry(bits_entry, state);
                setBitsBlock(dad_branch->partial_pars, ptn, bits_entry);
            } else {
                // ambiguous character, for DNA, RNA
                state = state - (nstates - 1);
                memset(bits_entry, 0, sizeof(UINT) * entry_size);
                bits_entry[0] = state;
                setBitsBlock(dad_branch->partial_pars, ptn, bits_entry);
            }
        }
    } else {
        // internal node
        //memset(dad_branch->partial_pars, 127, pars_size * sizeof(int));
        UINT *partial_pars_dad = dad_branch->partial_pars;
        UINT *partial_pars_child1 = NULL, *partial_pars_child2 = NULL;
        // take the intersection of two child states (with &= bit operation)
        FOR_NEIGHBOR_IT(node, dad, it)
        if ((*it)->node->name != ROOT_NAME) {
            computePartialParsimony((PhyloNeighbor*) (*it), (PhyloNode*) node);
            if (!partial_pars_child1)
                partial_pars_child1 = ((PhyloNeighbor*) (*it))->partial_pars;
            else
                partial_pars_child2 = ((PhyloNeighbor*) (*it))->partial_pars;
        }
        assert(partial_pars_child1 && partial_pars_child2);
        // take the intersection of two bits block
        for (int i = 0; i < pars_size - 1; i++)
            partial_pars_dad[i] = partial_pars_child1[i] & partial_pars_child2[i];
        int partial_pars = partial_pars_child1[pars_size - 1] + partial_pars_child2[pars_size - 1];
        // now check if some intersection is empty, change to union (Fitch algorithm) and increase the parsimony score
        for (ptn = 0; ptn < aln->size(); ptn++) {
            getBitsBlock(partial_pars_dad, ptn, bits_entry);
            if (isEmptyBitsEntry(bits_entry)) {
                getBitsBlock(partial_pars_child1, ptn, bits_entry1);
                getBitsBlock(partial_pars_child2, ptn, bits_entry2);
                unionBitsEntry(bits_entry1, bits_entry2, bits_entry);
                //cout << bits_entry[0] << " " << bits_entry[1] << endl;
                setBitsBlock(partial_pars_dad, ptn, bits_entry);
                partial_pars += aln->at(ptn).frequency;
            }
        }
        partial_pars_dad[pars_size - 1] = partial_pars;
    }
    dad_branch->partial_lh_computed |= 2;
    delete [] bits_entry2;
    delete [] bits_entry1;
    delete [] bits_entry;
}

int PhyloTree::computeParsimonyBranch(PhyloNeighbor *dad_branch, PhyloNode *dad) {
    PhyloNode *node = (PhyloNode*) dad_branch->node;
    PhyloNeighbor *node_branch = (PhyloNeighbor*) node->findNeighbor(dad);
    assert(node_branch);
    if (!central_partial_pars)
        initializeAllPartialLh();
    // swap node and dad if dad is a leaf
    if (node->isLeaf()) {
        PhyloNode *tmp_node = dad;
        dad = node;
        node = tmp_node;
        PhyloNeighbor *tmp_nei = dad_branch;
        dad_branch = node_branch;
        node_branch = tmp_nei;
        //cout << "swapped\n";
    }
    if ((dad_branch->partial_lh_computed & 2) == 0)
        computePartialParsimony(dad_branch, dad);
    if ((node_branch->partial_lh_computed & 2) == 0)
        computePartialParsimony(node_branch, node);
    // now combine likelihood at the branch

    int pars_size = getBitsBlockSize();
    int entry_size = getBitsEntrySize();
    //int nstates = aln->num_states;
    int i, ptn;
    int tree_pars = node_branch->partial_pars[pars_size - 1] + dad_branch->partial_pars[pars_size - 1];
    UINT *partial_pars = newBitsBlock();
    UINT *bits_entry = new UINT[entry_size];
    for (i = 0; i < pars_size - 1; i++)
        partial_pars[i] = (node_branch->partial_pars[i] & dad_branch->partial_pars[i]);

    for (ptn = 0; ptn < aln->size(); ptn++) {
        /*
        for (i = 0; i < aln->getNSeq(); i++)
                cout << aln->convertStateBack(aln->at(ptn)[i]);
        cout << endl;*/
        getBitsBlock(partial_pars, ptn, bits_entry);
        if (isEmptyBitsEntry(bits_entry))
            tree_pars += aln->at(ptn).frequency;
    }
    delete [] bits_entry;
    delete [] partial_pars;
    return tree_pars;
}

int PhyloTree::computeParsimony() {
    return computeParsimonyBranch((PhyloNeighbor*) root->neighbors[0], (PhyloNode*) root);
}

void PhyloTree::printParsimonyStates(PhyloNeighbor *dad_branch, PhyloNode *dad) {
    if (!dad) {
        dad = (PhyloNode*)root;
        dad_branch = (PhyloNeighbor*)root->neighbors[0];
        cout << "Parsimonious states for every node and site: " << endl;
    }
    int site;
    cout << "States for node ";
    int max_len = aln->getMaxSeqNameLength();
    if (max_len < 3) max_len = 3;
    cout.width(max_len);
    if (!dad_branch->node->name.empty())
        cout << left << dad_branch->node->name;
    else
        cout << left << dad_branch->node->id;
    cout << " are ";
    UINT *bits_entry = new UINT[getBitsEntrySize()];
    for (site = 0; site < aln->getNSite(); site++) {
        int ptn = aln->getPatternID(site);
        getBitsBlock(dad_branch->partial_pars, ptn, bits_entry);
        cout << "{";
        bool first = true;
        for (int i = 0; i < aln->num_states; i++)
            if (getBitsEntry(bits_entry, i)) {
                cout << ((!first) ? "," : "") << i;
                first = false;
            }
        cout << "}\t";
    }
    cout << endl;
    delete [] bits_entry;
    FOR_NEIGHBOR_IT(dad_branch->node, dad, it)
    printParsimonyStates((PhyloNeighbor*)(*it), (PhyloNode*)(dad_branch->node));
}

int PhyloTree::computeParsimonyScore(int ptn, int &states, PhyloNode *node, PhyloNode *dad) {
    int score = 0;
    states = 0;
    if (!node) node = (PhyloNode*) root;
    if (node->degree() > 3)
        outError("Does not work with multifurcating tree");
    if (verbose_mode == VB_DEBUG)
        cout << ptn << " " << node->id << "  " << node->name << endl;

    if (node->isLeaf()) {
        char state;
        if (node->name == ROOT_NAME) {
            state = STATE_UNKNOWN;
        } else {
            assert(node->id < aln->getNSeq());
            state = (*aln)[ptn][node->id];
        }
        if (state == STATE_UNKNOWN) {
            states = (1 << aln->num_states) - 1;
        } else if (state < aln->num_states)
            states = (1 << state);
        else {
            // ambiguous character, for DNA, RNA
            states = state - 3;
        }
    }
    if (!node->isLeaf() || node == root) {
        int union_states = 0;
        int intersect_states = (1 << aln->num_states) - 1;
        if (states != 0) {
            union_states = states;
            intersect_states = states;
        }

        FOR_NEIGHBOR_IT(node, dad, it) {
            int states_child;
            int score_child = computeParsimonyScore(ptn, states_child, (PhyloNode*) ((*it)->node), node);
            union_states |= states_child;
            intersect_states &= states_child;
            score += score_child;
        }
        if (intersect_states)
            states = intersect_states;
        else {
            states = union_states;
            score++;
        }
    }
    return score;
}

int PhyloTree::computeParsimonyScore() {
    assert(root && root->isLeaf());

    int score = 0;
    for (int ptn = 0; ptn < aln->size(); ptn++)
        if (!aln->at(ptn).is_const) {
            int states;
            score += computeParsimonyScore(ptn, states) * (*aln)[ptn].frequency;
        }
    return score;
}

/****************************************************************************
        Nearest Neighbor Interchange with parsimony
 ****************************************************************************/

double PhyloTree::swapNNI(double cur_score, PhyloNode *node1, PhyloNode *node2) {
    assert(node1->degree() == 3 && node2->degree() == 3);
    FOR_NEIGHBOR_DECLARE(node1, node2, it1)
    break;
    Node *node1_nei = (*it1)->node;

    FOR_NEIGHBOR_IT(node2, node1, it2) {
        // do the NNI swap
        Node *node2_nei = (*it2)->node;
        node1->updateNeighbor(node1_nei, node2_nei);
        node1_nei->updateNeighbor(node1, node2);
        node2->updateNeighbor(node2_nei, node1_nei);
        node2_nei->updateNeighbor(node2, node1);

        // compute the score of the swapped topology
        double score = computeParsimonyScore();
        // if better: return
        if (score < cur_score) return score;
        // else, swap back
        node1->updateNeighbor(node2_nei, node1_nei);
        node1_nei->updateNeighbor(node2, node1);
        node2->updateNeighbor(node1_nei, node2_nei);
        node2_nei->updateNeighbor(node1, node2);
    }
    return cur_score;
}

double PhyloTree::searchNNI(double cur_score, PhyloNode *node, PhyloNode *dad) {
    if (!node) node = (PhyloNode*) root;
    if (!node->isLeaf() && dad && !dad->isLeaf()) {
        double score = swapNNI(cur_score, node, dad);
        if (score < cur_score) return score;
    }

    FOR_NEIGHBOR_IT(node, dad, it) {
        double score = searchNNI(cur_score, (PhyloNode*) (*it)->node, node);
        if (score < cur_score) return score;
    }
    return cur_score;
}

void PhyloTree::searchNNI() {
    cout << "Search with Nearest Neighbor Interchange..." << endl;
    double cur_score = computeParsimonyScore();
    do {
        double score = searchNNI(cur_score);
        if (score >= cur_score) break;
        cout << "Better score found: " << score << endl;
        cur_score = score;
    } while (true);
}

/****************************************************************************
        Stepwise addition (greedy) by maximum parsimony
 ****************************************************************************/

int PhyloTree::addTaxonMP(Node *added_node, Node* &target_node, Node* &target_dad, Node *node, Node *dad) {
    Neighbor *dad_nei = dad->findNeighbor(node);

    // now insert the new node in the middle of the branch node-dad
    double len = dad_nei->length;
    node->updateNeighbor(dad, added_node, len / 2.0);
    dad->updateNeighbor(node, added_node, len / 2.0);
    added_node->updateNeighbor((Node*) 1, node, len / 2.0);
    added_node->updateNeighbor((Node*) 2, dad, len / 2.0);
    // compute the likelihood
    //clearAllPartialLh();
    int best_score = computeParsimonyScore();
    target_node = node;
    target_dad = dad;
    // remove the added node
    node->updateNeighbor(added_node, dad, len);
    dad->updateNeighbor(added_node, node, len);
    added_node->updateNeighbor(node, (Node*) 1, len);
    added_node->updateNeighbor(dad, (Node*) 2, len);

    // now tranverse the tree downwards

    FOR_NEIGHBOR_IT(node, dad, it) {
        Node *target_node2;
        Node *target_dad2;
        double score = addTaxonMP(added_node, target_node2, target_dad2, (*it)->node, node);
        if (score < best_score) {
            best_score = score;
            target_node = target_node2;
            target_dad = target_dad2;
        }
    }
    return best_score;
}

void PhyloTree::growTreeMP(Alignment *alignment) {

    cout << "Stepwise addition using maximum parsimony..." << endl;
    aln = alignment;
    int size = aln->getNSeq();
    if (size < 3)
        outError(ERR_FEW_TAXA);

    root = newNode();
    Node *new_taxon;

    // create initial tree with 3 taxa
    for (leafNum = 0; leafNum < 3; leafNum++) {
        if (verbose_mode >= VB_MAX)
            cout << "Add " << aln->getSeqName(leafNum) << " to the tree" << endl;
        new_taxon = newNode(leafNum, aln->getSeqName(leafNum).c_str());
        root->addNeighbor(new_taxon, 1.0);
        new_taxon->addNeighbor(root, 1.0);
    }
    root = findNodeID(0);
    //optimizeAllBranches();

    // stepwise adding the next taxon
    for (leafNum = 3; leafNum < size; leafNum++) {
        if (verbose_mode >= VB_MAX)
            cout << "Add " << aln->getSeqName(leafNum) << " to the tree";
        // allocate a new taxon and a new ajedcent internal node
        new_taxon = newNode(leafNum, aln->getSeqName(leafNum).c_str());
        Node *added_node = newNode();
        added_node->addNeighbor(new_taxon, 1.0);
        new_taxon->addNeighbor(added_node, 1.0);

        // preserve two neighbors
        added_node->addNeighbor((Node*) 1, 1.0);
        added_node->addNeighbor((Node*) 2, 1.0);


        Node *target_node = NULL;
        Node *target_dad = NULL;
        int score = addTaxonMP(added_node, target_node, target_dad, root->neighbors[0]->node, root);
        if (verbose_mode >= VB_MAX)
            cout << ", score = " << score << endl;
        // now insert the new node in the middle of the branch node-dad
        double len = target_dad->findNeighbor(target_node)->length;
        target_node->updateNeighbor(target_dad, added_node, len / 2.0);
        target_dad->updateNeighbor(target_node, added_node, len / 2.0);
        added_node->updateNeighbor((Node*) 1, target_node, len / 2.0);
        added_node->updateNeighbor((Node*) 2, target_dad, len / 2.0);
        // compute the likelihood
        //clearAllPartialLh();
        //optimizeAllBranches();
        //optimizeNNI();
    }

    nodeNum = 2 * leafNum - 2;
}

/****************************************************************************
        likelihood function
 ****************************************************************************/

void PhyloTree::initializeAllPartialLh() {
    int index;
    int mem_size = ((alnSize % 2) == 0) ? alnSize : (alnSize+1);
    block_size = mem_size * numStates * site_rate->getNRate();
    //block_size = alnSize * numStates * site_rate->getNRate();
    if (!tmp_partial_lh1)
        tmp_partial_lh1 = newPartialLh();
    if (!tmp_partial_lh2)
        tmp_partial_lh2 = newPartialLh();
    if (!tmp_scale_num1)
        tmp_scale_num1 = newScaleNum();
    if (!tmp_scale_num2)
        tmp_scale_num2 = newScaleNum();
    if (!_pattern_lh) _pattern_lh = new double [aln->size()];
    initializeAllPartialLh(index);
    assert(index == (nodeNum - 1)*2);
}

void PhyloTree::initializeAllPartialLh(int &index, PhyloNode *node, PhyloNode *dad) {
    int pars_block_size = getBitsBlockSize();
    int scale_block_size = aln->size();
    if (!node) {
        node = (PhyloNode*) root;
        // allocate the big central partial likelihoods memory
        if (!central_partial_lh) {
			int64_t mem_size = ((int64_t)leafNum - 1) * 4 * (int64_t)block_size + 2;
            //if (verbose_mode >= VB_MIN)
                cout << "Note: Requiring " <<  (double)mem_size * sizeof (double) / (1024*1024) << " MB memory for partial likelihoods" << endl;
			if (mem_size >= getTotalSystemMemory())
				outWarning("Degrade performance due to smaller RAM size, please switch to another computer with larger RAM");
            central_partial_lh = new double[mem_size];
            if (!central_partial_lh)
                outError("Not enough memory for partial likelihood vectors");

        }
        if (!central_scale_num) {
            if (verbose_mode >= VB_MED)
                cout << "Allocating " << (leafNum - 1)*4*scale_block_size*sizeof (UBYTE) << " bytes for scale num vectors" << endl;
            central_scale_num = new UBYTE[(leafNum-1)*4*scale_block_size];
            if (!central_scale_num)
                outError("Not enough memory for scale num vectors");
        }
        if (!central_partial_pars) {
            if (verbose_mode >= VB_MED)
                cout << "Allocating " << (leafNum - 1)*4 * pars_block_size * sizeof (UINT) << " bytes for partial parsimony vectors" << endl;
            central_partial_pars = new UINT[(leafNum-1)*4*pars_block_size];
            if (!central_partial_pars)
                outError("Not enough memory for partial parsimony vectors");
        }
        index = 0;
    }
    if (dad) {
        // make memory alignment 16
		int mem_shift = 0;
		if (((intptr_t)central_partial_lh) % 16 != 0) mem_shift = 1;
        // assign a region in central_partial_lh to both Neihgbors (dad->node, and node->dad)
        PhyloNeighbor *nei = (PhyloNeighbor*) node->findNeighbor(dad);
        //assert(!nei->partial_lh);
        nei->partial_lh = central_partial_lh + (index * block_size + mem_shift);
        nei->scale_num = central_scale_num + (index * scale_block_size);
        nei->partial_pars = central_partial_pars + (index * pars_block_size);
        nei = (PhyloNeighbor*) dad->findNeighbor(node);
        //assert(!nei->partial_lh);
        nei->partial_lh = central_partial_lh + ((index + 1) * block_size + mem_shift);
        nei->scale_num = central_scale_num + ((index + 1) * scale_block_size);
        nei->partial_pars = central_partial_pars + ((index + 1) * pars_block_size);
        index += 2;
        assert(index < nodeNum * 2 - 1);
    }
    FOR_NEIGHBOR_IT(node, dad, it)
    initializeAllPartialLh(index, (PhyloNode*) (*it)->node, node);
}

double *PhyloTree::newPartialLh() {
	double *ret = new double[aln->size() * aln->num_states * site_rate->getNRate() + 2];
    return ret;
}

UBYTE *PhyloTree::newScaleNum() {
    return new UBYTE[aln->size()];
}

double PhyloTree::computeLikelihood(double *pattern_lh) {
    assert(model);
    assert(site_rate);
    assert(root->isLeaf());
    //double *ptn_scale = NULL;
    PhyloNeighbor *nei = ((PhyloNeighbor*) root->neighbors[0]);
    current_it = nei;
    assert(current_it);
    current_it_back = (PhyloNeighbor*) nei->node->findNeighbor(root);
    assert(current_it_back);
//    double sum_scaling = nei->lh_scale_factor;
//    double check = 0.0;
    /*    if (pattern_lh) {
            if (sum_scaling < 0.0) {
                ptn_scale = new double[nptn];
                memset(ptn_scale, 0, sizeof (double) * nptn);
                for (int i = 0; i < nptn; i++) {
                	ptn_scale[i] = max(nei->scale_num[i],UBYTE(0)) * LOG_SCALING_THRESHOLD;
                	check += ptn_scale[i] * aln->at(i).frequency;
                }
                if (fabs(check-sum_scaling) > 1e-3) {
                	outError("sum_scaling does not match", __func__);
                }
                //clearAllPartialLh();
            }
        }*/
    double score = computeLikelihoodBranch(nei, (PhyloNode*) root, pattern_lh);
    if (pattern_lh && nei->lh_scale_factor < 0.0) {
        int nptn = aln->getNPattern();
        //double check_score = 0.0;
        for (int i = 0; i < nptn; i++) {
            pattern_lh[i] += max(nei->scale_num[i],UBYTE(0)) * LOG_SCALING_THRESHOLD;
            //check_score += (pattern_lh[i] * (aln->at(i).frequency));
        }
        /*       if (fabs(score - check_score) > 1e-6) {
               	cout << "score = " << score << " check_score = " << check_score << endl;
               	outError("Scaling error ", __func__);
               }*/
        //delete [] ptn_scale;
    }
    return score;
}

void PhyloTree::computePatternLikelihood(double *ptn_lh, double *cur_logl) {
    /*	if (!dad_branch) {
    		dad_branch = (PhyloNeighbor*) root->neighbors[0];
    		dad = (PhyloNode*) root;
    	}*/
    int nptn = aln->getNPattern();
    //PhyloNeighbor *node_branch = (PhyloNeighbor*)dad_branch->node->findNeighbor(dad);
    //double sum_scaling = dad_branch->lh_scale_factor + node_branch->lh_scale_factor;
    double sum_scaling = current_it->lh_scale_factor + current_it_back->lh_scale_factor;
    if (sum_scaling < 0.0) {
        for (int i = 0; i < nptn; i++) {
            //ptn_lh[i] = _pattern_lh[i] + (max(UBYTE(0),dad_branch->scale_num[i])+max(UBYTE(0),node_branch->scale_num[i])) * LOG_SCALING_THRESHOLD;
            ptn_lh[i] = _pattern_lh[i] + (max(UBYTE(0),current_it->scale_num[i])+max(UBYTE(0),current_it_back->scale_num[i])) * LOG_SCALING_THRESHOLD;
        }

        /*
        		double *ptn_scale = new double[nptn];
        		memset(ptn_scale, 0, sizeof (double) * nptn);
        		if (dad_branch->lh_scale_factor != 0.0) {
        			dad_branch->clearForwardPartialLh(dad);
        			computePartialLikelihood(dad_branch, dad, ptn_scale);
        		}
        		if (node_branch->lh_scale_factor != 0.0) {
        			node_branch->clearForwardPartialLh(dad_branch->node);
        			computePartialLikelihood(node_branch, (PhyloNode*)dad_branch->node, ptn_scale);
        		}
                for (int i = 0; i < nptn; i++) {
                    _pattern_lh[i] += ptn_scale[i];
                }
                delete [] ptn_scale;
        */
    } else
        memmove(ptn_lh, _pattern_lh, nptn*sizeof(double));
    if (cur_logl) {
        double check_score = 0.0;
        for (int i = 0; i < nptn; i++) {
            check_score += (ptn_lh[i] * (aln->at(i).frequency));
        }
        if (fabs(check_score - *cur_logl) > 0.001) {
            cout << *cur_logl << " " << check_score << endl;
            outError("Wrong ", __func__);
        }
    }
    //double score = computeLikelihoodBranch(dad_branch, dad, pattern_lh);
    //return score;
}

double PhyloTree::computeLogLVariance(double *ptn_lh, double tree_lh) {
    int i;
    int nptn = getAlnNPattern();
    int nsite = getAlnNSite();
    double *pattern_lh = ptn_lh;
    if (!ptn_lh) {
        pattern_lh = new double[nptn];
        computePatternLikelihood(pattern_lh);
    }
    IntVector pattern_freq;
    aln->getPatternFreq(pattern_freq);
    if (tree_lh == 0.0) {
        for (i = 0; i < nptn; i++) tree_lh += pattern_lh[i] * pattern_freq[i];
    }
    double avg_site_lh = tree_lh / nsite;
    double variance = 0.0;
    for (i = 0; i < nptn; i++) {
        double diff = (pattern_lh[i] - avg_site_lh);
        variance +=  diff * diff * pattern_freq[i];
    }
    if (!ptn_lh) delete [] pattern_lh;
    return variance * (nsite / (nsite-1));
}

double PhyloTree::computeLogLDiffVariance(double *pattern_lh_other, double *ptn_lh) {
    int i;
    int nptn = getAlnNPattern();
    int nsite = getAlnNSite();
    double *pattern_lh = ptn_lh;
    if (!ptn_lh) {
        pattern_lh = new double[nptn];
        computePatternLikelihood(pattern_lh);
    }
    IntVector pattern_freq;
    aln->getPatternFreq(pattern_freq);
    
    double avg_site_lh_diff = 0.0;
    for (i = 0; i < nptn; i++)
        avg_site_lh_diff += (pattern_lh[i]-pattern_lh_other[i]) * pattern_freq[i];
    avg_site_lh_diff /= nsite;
    double variance = 0.0;
    for (i = 0; i < nptn; i++) {
        double diff = (pattern_lh[i]- pattern_lh_other[i] - avg_site_lh_diff);
        variance += diff * diff * pattern_freq[i];
    }
    if (!ptn_lh) delete [] pattern_lh;
    return variance * (nsite / (nsite-1));
}

double PhyloTree::computeLogLDiffVariance(PhyloTree *other_tree, double *pattern_lh) {
    double *pattern_lh_other = new double[getAlnNPattern()];
    other_tree->computePatternLikelihood(pattern_lh_other);
    return computeLogLDiffVariance(pattern_lh_other, pattern_lh);
    delete [] pattern_lh_other;
}

double PhyloTree::computeLikelihoodBranchNaive(PhyloNeighbor *dad_branch, PhyloNode *dad, double *pattern_lh, double *pattern_rate) {
    PhyloNode *node = (PhyloNode*) dad_branch->node;
    PhyloNeighbor *node_branch = (PhyloNeighbor*) node->findNeighbor(dad);
    assert(node_branch);
	assert(!site_rate->isSiteSpecificRate() || !model->isSiteSpecificModel());
    if (!central_partial_lh)
        initializeAllPartialLh();
    // swap node and dad if dad is a leaf
    if (node->isLeaf()) {
        PhyloNode *tmp_node = dad;
        dad = node;
        node = tmp_node;
        PhyloNeighbor *tmp_nei = dad_branch;
        dad_branch = node_branch;
        node_branch = tmp_nei;
        //cout << "swapped\n";
    }
    if ((dad_branch->partial_lh_computed & 1) == 0)
        computePartialLikelihood(dad_branch, dad);
    if ((node_branch->partial_lh_computed & 1) == 0)
        computePartialLikelihood(node_branch, node);
    // now combine likelihood at the branch

    double tree_lh = node_branch->lh_scale_factor + dad_branch->lh_scale_factor;
    int ncat = site_rate->getNRate();
    double p_invar = site_rate->getPInvar();
    double p_var_cat = (1.0 - p_invar) / (double) ncat;
    int nstates = aln->num_states;
    int block = ncat * nstates;
    int trans_size = model->getTransMatrixSize();
    int ptn, cat, state1, state2;
	int nptn = aln->size();
	int discrete_cat = site_rate->getNDiscreteRate();
    double *trans_mat = new double[discrete_cat * trans_size];
    double *state_freq = new double[nstates];
    model->getStateFrequency(state_freq);

    if (!site_rate->isSiteSpecificRate())
        for (cat = 0; cat < discrete_cat; cat++) {
            //trans_mat[cat] = model->newTransMatrix();
            double *trans_cat = trans_mat + (cat * trans_size);
            model_factory->computeTransMatrixFreq(dad_branch->length * site_rate->getRate(cat), state_freq, trans_cat);
        }


    bool not_ptn_cat = (site_rate->getPtnCat(0) < 0);


#ifdef _OPENMP
#pragma omp parallel for reduction(+: tree_lh) private(ptn, cat, state1, state2)
#endif
    for (ptn = 0; ptn < nptn; ptn++) {
        double lh_ptn = 0.0; // likelihood of the pattern
        double rate_ptn = 0.0;
        int dad_state = 1000; // just something big enough
        int ptn_cat = site_rate->getPtnCat(ptn);
        if (dad->isLeaf())
            dad_state = (*aln)[ptn][dad->id];
        int dad_offset = dad_state * nstates;
        if (site_rate->isSiteSpecificRate())
            model_factory->computeTransMatrixFreq(dad_branch->length * site_rate->getPtnRate(ptn), state_freq, trans_mat);
        for (cat = 0; cat < ncat; cat++) {
            double lh_cat = 0.0; // likelihood of the pattern's category
            int lh_offset = cat * nstates + ptn*block;
            double *partial_lh_site = node_branch->partial_lh + lh_offset;
            double *partial_lh_child = dad_branch->partial_lh + lh_offset;
            if (dad_state < nstates) { // single state
                // external node
                double *trans_state = trans_mat + ((not_ptn_cat ? cat : ptn_cat) * trans_size + dad_offset);
				if (model->isSiteSpecificModel()) trans_state += (nstates * nstates * model->getPtnModelID(ptn));
                for (state2 = 0; state2 < nstates; state2++)
                    lh_cat += partial_lh_child[state2] * trans_state[state2];
            } else {
                // internal node, or external node but ambiguous character
                for (state1 = 0; state1 < nstates; state1++) {
                    double lh_state = 0.0; // likelihood of state1
                    double *trans_state = trans_mat + ((not_ptn_cat ? cat : ptn_cat) * trans_size + state1 * nstates);
					if (model->isSiteSpecificModel()) trans_state += (nstates * nstates * model->getPtnModelID(ptn));
                    for (state2 = 0; state2 < nstates; state2++)
                        lh_state += partial_lh_child[state2] * trans_state[state2];
                    lh_cat += lh_state * partial_lh_site[state1];
                }
            }
            lh_ptn += lh_cat;
            if (pattern_rate) rate_ptn += lh_cat * site_rate->getRate(cat);
        }
        if (pattern_rate) pattern_rate[ptn] = rate_ptn / lh_ptn;
        lh_ptn *= p_var_cat;
        if ((*aln)[ptn].is_const && (*aln)[ptn][0] < nstates) {
            lh_ptn += p_invar * state_freq[(int) (*aln)[ptn][0]];
        }
        //#ifdef DEBUG
        if (lh_ptn <= 0.0)
            cout << "Negative likelihood: " << lh_ptn << " " << site_rate->getPtnRate(ptn) << endl;
        //#endif
        assert(lh_ptn > 0);
        lh_ptn = log(lh_ptn);
        _pattern_lh[ptn] = lh_ptn;
        if (discard_saturated_site && site_rate->isSiteSpecificRate() && site_rate->getPtnRate(ptn) >= MAX_SITE_RATE)
            continue;
        tree_lh += lh_ptn * aln->at(ptn).frequency;
    }
    if (pattern_lh) memmove(pattern_lh, _pattern_lh, aln->size()*sizeof(double));
    delete [] state_freq;
    delete [] trans_mat;
    //for (cat = ncat-1; cat >= 0; cat--)
    //delete trans_mat[cat];
    //delete state_freq;
    return tree_lh;
}

double PhyloTree::computeLikelihoodZeroBranch(PhyloNeighbor *dad_branch, PhyloNode *dad) {
	double lh_zero_branch;
	double saved_len = dad_branch->length;
	PhyloNeighbor *node_branch = (PhyloNeighbor*)dad_branch->node->findNeighbor(dad);
	dad_branch->length = 0.0;
	node_branch->length = 0.0;
	lh_zero_branch = computeLikelihoodBranch(dad_branch, dad);
	// restore branch length
	dad_branch->length = saved_len;
	node_branch->length = saved_len;
	return lh_zero_branch;
}

void PhyloTree::computePartialLikelihoodNaive(PhyloNeighbor *dad_branch, PhyloNode *dad, double *pattern_scale) {
    // don't recompute the likelihood
    if (dad_branch->partial_lh_computed & 1)
        return;
    Node *node = dad_branch->node;
    int ptn, cat;
    int ncat = site_rate->getNRate();
    int nstates = aln->num_states;
    int block = nstates * site_rate->getNRate();
    int trans_size = model->getTransMatrixSize();
    int lh_size = aln->size() * block;
    double *partial_lh_site;
    int nptn = aln->size();

    dad_branch->lh_scale_factor = 0.0;
    memset(dad_branch->scale_num, 0, aln->size() * sizeof (UBYTE));

    assert(dad_branch->partial_lh);
    //if (!dad_branch->partial_lh)
    //	dad_branch->partial_lh = newPartialLh();
    if (node->isLeaf() && dad) {
        /* external node */
        memset(dad_branch->partial_lh, 0, lh_size * sizeof (double));
        for (ptn = 0; ptn < nptn; ptn++) {
            char state;
            partial_lh_site = dad_branch->partial_lh + (ptn * block);
            if (node->name == ROOT_NAME) {
                state = STATE_UNKNOWN;
            } else {
                assert(node->id < aln->getNSeq());
                state = (aln->at(ptn))[node->id];
            }
            if (state == STATE_UNKNOWN) {
                // fill all entries (also over rate category) with 1.0
                dad_branch->scale_num[ptn] = -1;
                for (int state2 = 0; state2 < block; state2++) {
                    partial_lh_site[state2] = 1.0;
                }
            } else if (state < nstates) {
                for (cat = 0; cat < ncat; cat++)
                    partial_lh_site[cat * nstates + state] = 1.0;
            } else {
                // ambiguous character, for DNA, RNA
				if (verbose_mode >= VB_MED) cout << "Process ambiguous char " << (int)state << endl;
                state = state - (nstates - 1);
                for (int state2 = 0; state2 < nstates && state2 <= 6; state2++)
                    if (state & (1 << state2)) {
                        for (cat = 0; cat < ncat; cat++)
                            partial_lh_site[cat * nstates + state2] = 1.0;
                    }
            }
        }
    } else {
        /* internal node */
        int discrete_cat = site_rate->getNDiscreteRate();
        double *trans_mat = new double[discrete_cat * trans_size];
        //for (cat = 0; cat < discrete_cat; cat++) trans_mat[cat] = model->newTransMatrix();
        for (ptn = 0; ptn < lh_size; ptn++) {
            dad_branch->partial_lh[ptn] = 1.0;
        }
        for (ptn = 0; ptn < nptn; ptn++) dad_branch->scale_num[ptn] = -1;

        FOR_NEIGHBOR_IT(node, dad, it)
        if ((*it)->node->name != ROOT_NAME) {
            computePartialLikelihoodNaive((PhyloNeighbor*) (*it), (PhyloNode*) node, pattern_scale);

            dad_branch->lh_scale_factor += ((PhyloNeighbor*) (*it))->lh_scale_factor;

            if (!site_rate->isSiteSpecificRate())
                for (cat = 0; cat < discrete_cat; cat++)
                    model_factory->computeTransMatrix((*it)->length * site_rate->getRate(cat), trans_mat + (cat * trans_size));

            bool not_ptn_cat = (site_rate->getPtnCat(0) < 0);

			double sum_scale = 0.0;
#ifdef _OPENMP
			#pragma omp parallel for reduction(+: sum_scale) private(ptn, cat, partial_lh_site)
#endif
            for (ptn = 0; ptn < nptn; ptn++)
                if (((PhyloNeighbor*) (*it))->scale_num[ptn] >= 0) {
                    // avoid the case that all child partial likelihoods equal 1.0
                    //double *partial_lh_child = ((PhyloNeighbor*) (*it))->partial_lh + (ptn*block);
                    //if (partial_lh_child[0] < 0.0) continue;
                    //
                    if (dad_branch->scale_num[ptn] < 0) dad_branch->scale_num[ptn] = 0;
                    dad_branch->scale_num[ptn] += ((PhyloNeighbor*) (*it))->scale_num[ptn];
                    int ptn_cat = site_rate->getPtnCat(ptn);
                    if (site_rate->isSiteSpecificRate())
                        model_factory->computeTransMatrix((*it)->length * site_rate->getPtnRate(ptn), trans_mat);

                    for (cat = 0; cat < ncat; cat++) {
                        int lh_offset = cat * nstates + ptn*block;
                        partial_lh_site = dad_branch->partial_lh + lh_offset;
                        double *partial_lh_child = ((PhyloNeighbor*) (*it))->partial_lh + lh_offset;
                        for (int state = 0; state < nstates; state++) {
                            double lh_child = 0.0;
                            double *trans_state = trans_mat + ((not_ptn_cat ? cat : ptn_cat) * trans_size + state * nstates);
                            if (model->isSiteSpecificModel()) trans_state += (nstates * nstates * model->getPtnModelID(ptn));
                            for (int state2 = 0; state2 < nstates; state2++)
                                lh_child += trans_state[state2] * partial_lh_child[state2];

                            partial_lh_site[state] *= lh_child;
                        }
                    }
                    // check if one should scale partial likelihoods
                    bool do_scale = true;
                    partial_lh_site = dad_branch->partial_lh + (ptn * block);
                    for (cat = 0; cat < block; cat++)
                        if (partial_lh_site[cat] > SCALING_THRESHOLD) {
                            do_scale = false;
                            break;
                        }
                    if (!do_scale) continue;
                    // now do the likelihood scaling
                    /*
                    double lh_max = partial_lh_site[0];
                    for (cat = 1; cat < block; cat++)
                            if (lh_max < partial_lh_site[cat]) lh_max = partial_lh_site[cat];
                    for (cat = 0; cat < block; cat++)
                            partial_lh_site[cat] /= lh_max;
                    dad_branch->lh_scale_factor += log(lh_max) * (*aln)[ptn].frequency;

                     */
                    for (cat = 0; cat < block; cat++)
                        partial_lh_site[cat] /= SCALING_THRESHOLD;
                    sum_scale += LOG_SCALING_THRESHOLD * (*aln)[ptn].frequency;
                    dad_branch->scale_num[ptn] += 1;
                    if (pattern_scale)
                        pattern_scale[ptn] += LOG_SCALING_THRESHOLD;
                }
            dad_branch->lh_scale_factor += sum_scale;
        }
        delete [] trans_mat;
        //for (cat = ncat - 1; cat >= 0; cat--)
        //  delete [] trans_mat[cat];
    }
    dad_branch->partial_lh_computed |= 1;

}

double PhyloTree::computeLikelihoodDervNaive(PhyloNeighbor *dad_branch, PhyloNode *dad, double &df, double &ddf) {
    PhyloNode *node = (PhyloNode*) dad_branch->node;
    PhyloNeighbor *node_branch = (PhyloNeighbor*) node->findNeighbor(dad);
    assert(node_branch);
    // swap node and dad if dad is a leaf
    if (node->isLeaf()) {
        PhyloNode *tmp_node = dad;
        dad = node;
        node = tmp_node;
        PhyloNeighbor *tmp_nei = dad_branch;
        dad_branch = node_branch;
        node_branch = tmp_nei;
        //cout << "swapped\n";
    }
    if ((dad_branch->partial_lh_computed & 1) == 0)
        computePartialLikelihoodNaive(dad_branch, dad);
    if ((node_branch->partial_lh_computed & 1) == 0)
        computePartialLikelihoodNaive(node_branch, node);
    // now combine likelihood at the branch

    double tree_lh = node_branch->lh_scale_factor + dad_branch->lh_scale_factor;
    df = ddf = 0.0;
    int ncat = site_rate->getNRate();
    double p_invar = site_rate->getPInvar();
    double p_var_cat = (1.0 - p_invar) / (double) ncat;
    int nstates = aln->num_states;
    int block = ncat * nstates;
    int trans_size = model->getTransMatrixSize();
	int nptn = aln->size();
    int ptn, cat, state1, state2;

    int discrete_cat = site_rate->getNDiscreteRate();

    double *trans_mat = new double[discrete_cat * trans_size];
    double *trans_derv1 = new double[discrete_cat * trans_size];
    double *trans_derv2 = new double[discrete_cat * trans_size];
    double *state_freq = new double[nstates];
    model->getStateFrequency(state_freq);

    if (!site_rate->isSiteSpecificRate())
        for (cat = 0; cat < discrete_cat; cat++) {
            //trans_mat[cat] = model->newTransMatrix();
            double *trans_cat = trans_mat + (cat * trans_size);
            double *derv1_cat = trans_derv1 + (cat * trans_size);
            double *derv2_cat = trans_derv2 + (cat * trans_size);
            double rate_val = site_rate->getRate(cat);
            //double rate_sqr = rate_val * rate_val;
            model_factory->computeTransDervFreq(dad_branch->length, rate_val, state_freq, trans_cat, derv1_cat, derv2_cat);
            /*
            for (state1 = 0; state1 < nstates; state1++) {
            double *trans_mat_state = trans_cat + (state1 * nstates);
            double *trans_derv1_state = derv1_cat + (state1 * nstates);
            double *trans_derv2_state = derv2_cat + (state1 * nstates);

            for (state2 = 0; state2 < nstates; state2++) {
            trans_mat_state[state2] *= state_freq[state1];
            trans_derv1_state[state2] *= (state_freq[state1] * rate_val);
            trans_derv2_state[state2] *= (state_freq[state1] * rate_sqr);
            }
            }*/
        }

    bool not_ptn_cat = (site_rate->getPtnCat(0) < 0);
    double derv1_frac;
    double derv2_frac;

	double my_df = 0.0;
	double my_ddf = 0.0;
	
#ifdef _OPENMP
#pragma omp parallel for reduction(+: tree_lh, my_df, my_ddf) private(ptn, cat, state1, state2, derv1_frac, derv2_frac)
#endif
     for (ptn = 0; ptn < nptn; ptn++) {
        int ptn_cat = site_rate->getPtnCat(ptn);
        if (discard_saturated_site && site_rate->isSiteSpecificRate() && site_rate->getPtnRate(ptn) >= MAX_SITE_RATE)
            continue;
        double lh_ptn = 0.0; // likelihood of the pattern
        double lh_ptn_derv1 = 0.0;
        double lh_ptn_derv2 = 0.0;
        int dad_state = STATE_UNKNOWN;

        if (dad->isLeaf())
            dad_state = (*aln)[ptn][dad->id];
        int dad_offset = dad_state * nstates;
        if (site_rate->isSiteSpecificRate())
            model_factory->computeTransDervFreq(dad_branch->length, site_rate->getPtnRate(ptn), state_freq,
                                                trans_mat, trans_derv1, trans_derv2);
        for (cat = 0; cat < ncat; cat++) {
            int lh_offset = cat * nstates + ptn*block;
            double *partial_lh_site = node_branch->partial_lh + lh_offset;
            double *partial_lh_child = dad_branch->partial_lh + lh_offset;
            if (dad_state < nstates) {
                // external node
                int cat2 = (not_ptn_cat ? cat : ptn_cat) * trans_size + dad_offset;
				if (model->isSiteSpecificModel()) cat2 += (nstates * nstates * model->getPtnModelID(ptn));
                double *trans_state = trans_mat + cat2;
                double *derv1_state = trans_derv1 + cat2;
                double *derv2_state = trans_derv2 + cat2;
                for (state2 = 0; state2 < nstates; state2++) {
                    lh_ptn += partial_lh_child[state2] * trans_state[state2];
                    lh_ptn_derv1 += partial_lh_child[state2] * derv1_state[state2];
                    lh_ptn_derv2 += partial_lh_child[state2] * derv2_state[state2];
                }
            } else {
                // internal node, or external node but ambiguous character
                for (state1 = 0; state1 < nstates; state1++) {
                    double lh_state = 0.0; // likelihood of state1
                    double lh_state_derv1 = 0.0;
                    double lh_state_derv2 = 0.0;
                    int cat2 = (not_ptn_cat ? cat : ptn_cat) * trans_size + state1 * nstates;
					if (model->isSiteSpecificModel()) cat2 += (nstates * nstates * model->getPtnModelID(ptn));
                    double *trans_state = trans_mat + cat2;
                    double *derv1_state = trans_derv1 + cat2;
                    double *derv2_state = trans_derv2 + cat2;
                    for (state2 = 0; state2 < nstates; state2++) {
                        lh_state += partial_lh_child[state2] * trans_state[state2];
                        lh_state_derv1 += partial_lh_child[state2] * derv1_state[state2];
                        lh_state_derv2 += partial_lh_child[state2] * derv2_state[state2];
                    }
                    lh_ptn += lh_state * partial_lh_site[state1];
                    lh_ptn_derv1 += lh_state_derv1 * partial_lh_site[state1];
                    lh_ptn_derv2 += lh_state_derv2 * partial_lh_site[state1];
                }
            }
        }
        /*		if (p_invar > 0.0) {
        			lh_ptn *= p_var_cat;
        			lh_ptn_derv1 *= p_var_cat;
        			lh_ptn_derv2 *= p_var_cat;
        			if ((*aln)[ptn].is_const && (*aln)[ptn][0] < nstates) {
        				lh_ptn += p_invar * state_freq[(int) (*aln)[ptn][0]];
        			}
        			assert(lh_ptn > 0);
        			double derv1_frac = lh_ptn_derv1 / lh_ptn;
        			double derv2_frac = lh_ptn_derv2 / lh_ptn;
        			tree_lh += log(lh_ptn) * (*aln)[ptn].frequency;
        			df += derv1_frac * (*aln)[ptn].frequency;
        			ddf += (derv2_frac - derv1_frac * derv1_frac) * (*aln)[ptn].frequency;
        		} else {
        			double derv1_frac = lh_ptn_derv1 / lh_ptn;
        			double derv2_frac = lh_ptn_derv2 / lh_ptn;
        			lh_ptn *= p_var_cat;
        			assert(lh_ptn > 0);
        			tree_lh += log(lh_ptn) * (*aln)[ptn].frequency;
        			df += derv1_frac * (*aln)[ptn].frequency;
        			ddf += (derv2_frac - derv1_frac * derv1_frac) * (*aln)[ptn].frequency;

        		}
        */
        // Tung beo's trick

        lh_ptn = lh_ptn * p_var_cat;
        if ((*aln)[ptn].is_const && (*aln)[ptn][0] < nstates) {
            lh_ptn += p_invar * state_freq[(int) (*aln)[ptn][0]];
        }
        double pad = p_var_cat / lh_ptn;
        if (std::isinf(pad)) {
            lh_ptn_derv1 *= p_var_cat;
            lh_ptn_derv2 *= p_var_cat;
            derv1_frac = lh_ptn_derv1 / lh_ptn;
            derv2_frac = lh_ptn_derv2 / lh_ptn;
        } else {
            derv1_frac = lh_ptn_derv1 * pad;
            derv2_frac = lh_ptn_derv2 * pad;
        }
        double freq = (*aln)[ptn].frequency;
        double tmp1 = derv1_frac * freq;
        double tmp2 = derv2_frac * freq;
        my_df += tmp1;
        my_ddf += tmp2 - tmp1 * derv1_frac;
        lh_ptn = log(lh_ptn);
        tree_lh += lh_ptn * freq;
        _pattern_lh[ptn] = lh_ptn;
		if (!isfinite(lh_ptn) || !isfinite(my_df) || !isfinite(my_ddf)) {
			cout << "Abnormal " << __func__;
			abort();
		}

    }
    delete [] state_freq;
    delete [] trans_derv2;
    delete [] trans_derv1;
	delete [] trans_mat;
    //for (cat = ncat-1; cat >= 0; cat--)
    //delete trans_mat[cat];
    //delete state_freq;
    df = my_df;
    ddf = my_ddf;
    return tree_lh;
}

/****************************************************************************
        Branch length optimization by maximum likelihood
 ****************************************************************************/

double PhyloTree::computeFunction(double value) {
    current_it->length = value;
    current_it_back->length = value;
    return -computeLikelihoodBranch(current_it, (PhyloNode*) current_it_back->node);
}

double PhyloTree::computeFuncDerv(double value, double &df, double &ddf) {
    current_it->length = value;
    current_it_back->length = value;
    double lh = -computeLikelihoodDerv(current_it, (PhyloNode*) current_it_back->node, df, ddf);
    df = -df;
    ddf = -ddf;
    return lh;
}

double PhyloTree::optimizeOneBranch(PhyloNode *node1, PhyloNode *node2, bool clearLH) {
    double negative_lh;
    current_it = (PhyloNeighbor*) node1->findNeighbor(node2);
    assert(current_it);
    current_it_back = (PhyloNeighbor*) node2->findNeighbor(node1);
    assert(current_it_back);
    double current_len = current_it->length;
    double ferror, optx;
    if (optimize_by_newton) // Newton-Raphson method
        optx = minimizeNewton(MIN_BRANCH_LEN, current_len, MAX_BRANCH_LEN, TOL_BRANCH_LEN, negative_lh);
    	//optx = minimizeNewtonTung(MIN_BRANCH_LEN, current_len, MAX_BRANCH_LEN, TOL_BRANCH_LEN, negative_lh);
    else // Brent method
        optx = minimizeOneDimen(MIN_BRANCH_LEN, current_len, MAX_BRANCH_LEN, TOL_BRANCH_LEN, &negative_lh, &ferror);
    if (current_len == optx) // if nothing changes, return
        return -negative_lh;
    current_it->length = optx;
    current_it_back->length = optx;
    if (clearLH) {
        node1->clearReversePartialLh(node2);
        node2->clearReversePartialLh(node1);
    }
    return -negative_lh;
}

double PhyloTree::optimizeChildBranches(PhyloNode *node, PhyloNode *dad) {
    double tree_lh = 0.0;

    FOR_NEIGHBOR_IT(node, dad, it) {
        tree_lh = optimizeOneBranch((PhyloNode*) node, (PhyloNode*) (*it)->node);
    }
    return tree_lh;
}

double PhyloTree::optimizeAllBranches(PhyloNode *node, PhyloNode *dad) {
    //double tree_lh = optimizeChildBranches(node, dad);
    double tree_lh = -DBL_MAX;

    FOR_NEIGHBOR_IT(node, dad, it) {
        //if (!(*it)->node->isLeaf())
    	double new_tree_lh = optimizeAllBranches((PhyloNode*) (*it)->node, node);
    	/*
    	if (new_tree_lh < tree_lh)
    		cout << "Wrong " << __func__ << endl;
    		*/
    	tree_lh = new_tree_lh;
    }
    if (dad) tree_lh = optimizeOneBranch(node, dad);
    return tree_lh;
}

double PhyloTree::optimizeAllBranches(int iterations, double tolerance) {
    if (verbose_mode >= VB_MAX)
        cout << "Optimizing branch lenths (max " << iterations << " loops)..." << endl;
    double tree_lh = computeLikelihood();
    //cout << tree_lh << endl;
    for (int i = 0; i < iterations; i++) {
        double new_tree_lh = optimizeAllBranches((PhyloNode*) root);
        /*
        clearAllPartialLH();
        double new_tree_lh2 = computeLikelihood();
        if (fabs(new_tree_lh - new_tree_lh2) > TOL_LIKELIHOOD) {
        	cout << "Wrong " << new_tree_lh <<" "<< new_tree_lh2 << endl;
        	exit(1);
        }*/
        if (verbose_mode >= VB_MAX) {
            cout << "BRANCH LEN " << i + 1 << " : ";
            cout << new_tree_lh << endl;
        }
        if (new_tree_lh <= tree_lh + tolerance)
            return (new_tree_lh > tree_lh) ? new_tree_lh : tree_lh;
        tree_lh = new_tree_lh;
    }
    return tree_lh;
}

/****************************************************************************
        Stepwise addition (greedy) by maximum likelihood
 ****************************************************************************/

double PhyloTree::addTaxonML(Node *added_node, Node* &target_node, Node* &target_dad, Node *node, Node *dad) {
    Neighbor *dad_nei = dad->findNeighbor(node);

    // now insert the new node in the middle of the branch node-dad
    double len = dad_nei->length;
    node->updateNeighbor(dad, added_node, len / 2.0);
    dad->updateNeighbor(node, added_node, len / 2.0);
    added_node->updateNeighbor((Node*) 1, node, len / 2.0);
    added_node->updateNeighbor((Node*) 2, dad, len / 2.0);
    // compute the likelihood
    clearAllPartialLH();
    double best_score = optimizeChildBranches((PhyloNode*) added_node);
    target_node = node;
    target_dad = dad;
    // remove the added node
    node->updateNeighbor(added_node, dad, len);
    dad->updateNeighbor(added_node, node, len);
    added_node->updateNeighbor(node, (Node*) 1, len);
    added_node->updateNeighbor(dad, (Node*) 2, len);

    // now tranverse the tree downwards

    FOR_NEIGHBOR_IT(node, dad, it) {
        Node *target_node2;
        Node *target_dad2;
        double score = addTaxonML(added_node, target_node2, target_dad2, (*it)->node, node);
        if (score > best_score) {
            best_score = score;
            target_node = target_node2;
            target_dad = target_dad2;
        }
    }
    return best_score;
}

void PhyloTree::growTreeML(Alignment *alignment) {

    cout << "Stepwise addition using ML..." << endl;
    aln = alignment;
    int size = aln->getNSeq();
    if (size < 3)
        outError(ERR_FEW_TAXA);

    root = newNode();
    Node *new_taxon;

    // create initial tree with 3 taxa
    for (leafNum = 0; leafNum < 3; leafNum++) {
        cout << "Add " << aln->getSeqName(leafNum) << " to the tree" << endl;
        new_taxon = newNode(leafNum, aln->getSeqName(leafNum).c_str());
        root->addNeighbor(new_taxon, 1.0);
        new_taxon->addNeighbor(root, 1.0);
    }
    root = findNodeID(0);
    optimizeAllBranches();

    // stepwise adding the next taxon
    for (leafNum = 3; leafNum < size; leafNum++) {
        cout << "Add " << aln->getSeqName(leafNum) << " to the tree" << endl;
        // allocate a new taxon and a new ajedcent internal node
        new_taxon = newNode(leafNum, aln->getSeqName(leafNum).c_str());
        Node *added_node = newNode();
        added_node->addNeighbor(new_taxon, 1.0);
        new_taxon->addNeighbor(added_node, 1.0);

        // preserve two neighbors
        added_node->addNeighbor((Node*) 1, 1.0);
        added_node->addNeighbor((Node*) 2, 1.0);


        Node *target_node = NULL;
        Node *target_dad = NULL;
        addTaxonML(added_node, target_node, target_dad, root->neighbors[0]->node, root);
        // now insert the new node in the middle of the branch node-dad
        double len = target_dad->findNeighbor(target_node)->length;
        target_node->updateNeighbor(target_dad, added_node, len / 2.0);
        target_dad->updateNeighbor(target_node, added_node, len / 2.0);
        added_node->updateNeighbor((Node*) 1, target_node, len / 2.0);
        added_node->updateNeighbor((Node*) 2, target_dad, len / 2.0);
        // compute the likelihood
        clearAllPartialLH();
        optimizeAllBranches();
        optimizeNNI();
    }

    nodeNum = 2 * leafNum - 2;
}

/****************************************************************************
        Distance function
 ****************************************************************************/

double PhyloTree::computeDist(int seq1, int seq2, double initial_dist) {
    // if no model or site rate is specified, return JC distance
    if (initial_dist == 0.0)
        initial_dist = aln->computeDist(seq1, seq2);
    if (!model_factory || !site_rate) return initial_dist;

    // now optimize the distance based on the model and site rate
    AlignmentPairwise aln_pair(this, seq1, seq2);
    return aln_pair.optimizeDist(initial_dist);
}

double PhyloTree::correctDist(double *dist_mat) {
    int i, j, k, pos;
    int n = aln->getNSeq();
    int nsqr = n*n;
    // use Floyd algorithm to find shortest path between all pairs of taxa
    for (k = 0; k < n; k++)
        for (i = 0, pos = 0; i < n; i++)
            for (j = 0; j < n; j++, pos++) {
                double tmp = dist_mat[i*n+k] + dist_mat[k*n+j];
                if (dist_mat[pos] > tmp) dist_mat[pos] = tmp;
            }
    double longest_dist = 0.0;
    for (i = 0; i < nsqr; i++)
        if (dist_mat[i] > longest_dist) longest_dist = dist_mat[i];
    return longest_dist;
}

double PhyloTree::computeDist(double *dist_mat) {
    int nseqs = aln->getNSeq();
    int pos = 0;
    int num_pairs = nseqs * (nseqs-1) / 2;
    double longest_dist = 0.0;
	int *row_id = new int[num_pairs];
	int *col_id = new int[num_pairs];
	row_id[0] = 0;
	col_id[0] = 1;
    for (pos = 1; pos < num_pairs; pos++) {
		row_id[pos] = row_id[pos-1];
		col_id[pos] = col_id[pos-1]+1;
		if (col_id[pos] >= nseqs) {
			row_id[pos]++;
			col_id[pos] = row_id[pos]+1;
		}
	}
	// compute the upper-triangle of distance matrix
#ifdef _OPENMP
	#pragma omp parallel for private(pos)
#endif
	for (pos = 0; pos < num_pairs; pos++) {
		int seq1 = row_id[pos];
		int seq2 = col_id[pos];
		int sym_pos = seq1*nseqs + seq2;
		dist_mat[sym_pos] = computeDist(seq1, seq2, dist_mat[sym_pos]);
	}
	// copy upper-triangle into lower-triangle and set diagonal = 0
    for (int seq1 = 0; seq1 < nseqs; seq1++)
        for (int seq2 = 0; seq2 <= seq1; seq2++) {
			pos = seq1 * nseqs + seq2;
            if (seq1 == seq2)
                dist_mat[pos] = 0.0;
            else dist_mat[pos] = dist_mat[seq2 * nseqs + seq1];
            if (dist_mat[pos] > longest_dist)
                longest_dist = dist_mat[pos];
        }
    delete [] col_id;
    delete [] row_id;
    /*
        if (longest_dist > MAX_GENETIC_DIST * 0.99)
            outWarning("Some distances are saturated. Please check your alignment again");*/
    return correctDist(dist_mat);
}

double PhyloTree::computeDist(Params &params, Alignment *alignment, double* &dist_mat, string &dist_file) {
    double longest_dist = 0.0;
    aln = alignment;
    dist_file = params.out_prefix;
    if (!model_factory)
        dist_file += ".jcdist";
    else
        dist_file += ".mldist";

    if (!dist_mat) {
        dist_mat = new double[alignment->getNSeq() * alignment->getNSeq()];
        memset(dist_mat, 0, sizeof (double) * alignment->getNSeq() * alignment->getNSeq());
    }
    if (!params.dist_file) {
        longest_dist = computeDist(dist_mat);
        alignment->printDist(dist_file.c_str(), dist_mat);
    } else {
        longest_dist = alignment->readDist(params.dist_file, dist_mat);
        dist_file = params.dist_file;
    }
    return longest_dist;
}

double PhyloTree::computeObsDist(double *dist_mat) {
    int nseqs = aln->getNSeq();
    int pos = 0;
    double longest_dist = 0.0;
    for (int seq1 = 0; seq1 < nseqs; seq1++)
        for (int seq2 = 0; seq2 < nseqs; seq2++, pos++) {
            if (seq1 == seq2)
                dist_mat[pos] = 0.0;
            else if (seq2 > seq1) {
                dist_mat[pos] = aln->computeObsDist(seq1, seq2);
            } else dist_mat[pos] = dist_mat[seq2 * nseqs + seq1];
            if (dist_mat[pos] > longest_dist)
                longest_dist = dist_mat[pos];
        }
    return correctDist(dist_mat);
}

double PhyloTree::computeObsDist(Params &params, Alignment *alignment, double* &dist_mat, string &dist_file) {
    double longest_dist = 0.0;
    aln = alignment;
    dist_file = params.out_prefix;
    dist_file += ".obsdist";

    if (!dist_mat) {
        dist_mat = new double[alignment->getNSeq() * alignment->getNSeq()];
        memset(dist_mat, 0, sizeof (double) * alignment->getNSeq() * alignment->getNSeq());
    }
    longest_dist = computeObsDist(dist_mat);
    alignment->printDist(dist_file.c_str(), dist_mat);
    return longest_dist;
}

/****************************************************************************
        compute BioNJ tree, a more accurate extension of Neighbor-Joining
 ****************************************************************************/

void PhyloTree::computeBioNJ(Params &params, Alignment *alignment, string &dist_file) {
    string bionj_file = params.out_prefix;
    bionj_file += ".bionj";

    cout << "Computing BIONJ tree..." << endl;
    BioNj bionj;
    bionj.create(dist_file.c_str(), bionj_file.c_str());
    bool my_rooted = false;
    bool non_empty_tree = (root != NULL);
    if (root) freeNode();
    readTree(bionj_file.c_str(), my_rooted);
    if (non_empty_tree) initializeAllPartialLh();
    setAlignment(alignment);
}

int PhyloTree::fixNegativeBranch(double fixed_length, Node *node, Node *dad) {
    if (!node) node = root;
    int fixed = 0;

    FOR_NEIGHBOR_IT(node, dad, it) {
        if ((*it)->length <= 0.0) { // negative branch length detected
            if (verbose_mode == VB_DEBUG)
                cout << "Negative branch length " << (*it)->length << " was set to ";
            (*it)->length = fixed_length;
            if (verbose_mode == VB_DEBUG)
                cout << (*it)->length << endl;
            // set the backward branch length
            (*it)->node->findNeighbor(node)->length = (*it)->length;
            fixed++;
        }
        fixed += fixNegativeBranch(fixed_length, (*it)->node, node);
    }
    return fixed;
}

/****************************************************************************
        Nearest Neighbor Interchange by maximum likelihood
 ****************************************************************************/

double PhyloTree::doNNI(NNIMove move) {
    PhyloNode *node1 = move.node1;
    PhyloNode *node2 = move.node2;
    NeighborVec::iterator node1Nei_it = move.node1Nei_it;
    NeighborVec::iterator node2Nei_it = move.node2Nei_it;
    Neighbor *node1Nei = *(node1Nei_it);
    Neighbor *node2Nei = *(node2Nei_it);

    // TODO MINH
    /*	Node *nodeA = node1Nei->node;
    	Node *nodeB = node2Nei->node;

        NeighborVec::iterator nodeA_it = nodeA->findNeighborIt(node1);
        NeighborVec::iterator nodeB_it = nodeB->findNeighborIt(node2);
        Neighbor *nodeANei = *(nodeA_it);
        Neighbor *nodeBNei = *(nodeB_it);
    	*node1Nei_it = node2Nei;
    	*nodeB_it = nodeANei;
    	*node2Nei_it = node1Nei;
    	*nodeA_it = nodeBNei;*/
    // END TODO MINH

    assert(node1->degree() == 3 && node2->degree() == 3);
    // do the NNI swap
    node1->updateNeighbor(node1Nei_it, node2Nei);
    node2Nei->node->updateNeighbor(node2, node1);

    node2->updateNeighbor(node2Nei_it, node1Nei);
    node1Nei->node->updateNeighbor(node1, node2);

    // BQM check branch ID
    /*
    	if (node1->findNeighbor(nodeB)->id != nodeB->findNeighbor(node1)->id) {
    		cout << node1->findNeighbor(nodeB)->id << "<->" << nodeB->findNeighbor(node1)->id << endl;
    		cout << node1->id << "," << nodeB->id << endl;
    		outError("Wrong ID");
    	}
    	if (node2->findNeighbor(nodeA)->id != nodeA->findNeighbor(node2)->id) {
    		cout << node2->findNeighbor(nodeA)->id << "<->" << nodeA->findNeighbor(node2)->id << endl;
    		cout << node2->id << "," << nodeA->id << endl;
    		outError("Wrong ID");
    	}*/

    PhyloNeighbor *node12_it = (PhyloNeighbor*) node1->findNeighbor(node2); // return neighbor of node1 which points to node 2
    PhyloNeighbor *node21_it = (PhyloNeighbor*) node2->findNeighbor(node1); // return neighbor of node2 which points to node 1

    // clear partial likelihood vector
    node12_it->clearPartialLh();
    node21_it->clearPartialLh();

    node2->clearReversePartialLh(node1);
    node1->clearReversePartialLh(node2);

    //optimizeOneBranch(node1, node2);

    // Return likelihood score only for debugging, otherwise return 0
    //return computeLikelihood();
    return 0;
}

double PhyloTree::swapNNIBranch(double cur_score, PhyloNode *node1, PhyloNode *node2, SwapNNIParam *nni_param/*,
                                ostream *out, int brtype, ostream *out_lh, ostream *site_lh,
                                StringIntMap *treels, vector<double*> *treels_ptnlh, DoubleVector *treels_logl,
                                int *max_trees, double *logl_cutoff*/) {
    assert(node1->degree() == 3 && node2->degree() == 3);

    PhyloNeighbor *node12_it = (PhyloNeighbor*) node1->findNeighbor(node2);
    PhyloNeighbor *node21_it = (PhyloNeighbor*) node2->findNeighbor(node1);
    double node12_len = node12_it->length;

    // save the likelihood vector at the two ends of node1-node2
    double *node1_lh_save = node12_it->partial_lh;
    double *node2_lh_save = node21_it->partial_lh;
    UBYTE *node1_scale_save = node12_it->scale_num;
    UBYTE *node2_scale_save = node21_it->scale_num;
    double node1_lh_scale = node12_it->lh_scale_factor;
    double node2_lh_scale = node21_it->lh_scale_factor;

    node12_it->partial_lh = newPartialLh();
    node21_it->partial_lh = newPartialLh();
    node12_it->scale_num = newScaleNum();
    node21_it->scale_num = newScaleNum();

    // TUNG save the first found neighbor (2 Neighbor total) of node 1 (excluding node2) in node1_it
    FOR_NEIGHBOR_DECLARE(node1, node2, node1_it)
    break;
    if (nni_param) node1_it = node1->findNeighborIt(nni_param->node1_nei->node);
    Neighbor *node1_nei = *node1_it;
    double node1_len = node1_nei->length;
    NeighborVec::iterator node1_nei_it = node1_nei->node->findNeighborIt(node1);


    // Neighbors of node2 which are not node1

    vector<NeighborVec::iterator> node2_its;
    FOR_NEIGHBOR_DECLARE(node2, node1, node2_it) node2_its.push_back(node2_it);
    assert(node2_its.size() == 2);
    if (nni_param && nni_param->node2_nei != (*node2_its[0])) {
        node2_it = node2_its[0];
        node2_its[0] = node2_its[1];
        node2_its[1] = node2_it;
    }

    int cnt;
    for (cnt = 0; cnt < node2_its.size(); cnt++) {
        node2_it = node2_its[cnt];
        // do the NNI swap
        Neighbor *node2_nei = *node2_it;
        // TUNG unused variable ?
        NeighborVec::iterator node2_nei_it = node2_nei->node->findNeighborIt(node2);

        double node2_len = node2_nei->length;

        node1->updateNeighbor(node1_it, node2_nei);
        node2_nei->node->updateNeighbor(node2, node1);

        node2->updateNeighbor(node2_it, node1_nei);
        node1_nei->node->updateNeighbor(node1, node2);
        bool duplicated_tree = false;
        string tree_str;
        /*if (treels) {
            stringstream ostr;
            printTree(ostr, WT_TAXON_ID | WT_SORT_TAXA);
            tree_str = ostr.str();
            StringIntMap::iterator it = treels->find(tree_str);
            if (it != treels->end()) {// already in treels
                duplicated_tree = true;
            } else {
                //cout << __func__ << ": new tree" << endl;
            }
        }*/

        double score = 0;

        if (!duplicated_tree) {
            // partial_lhclear partial likelihood vector
            node12_it->clearPartialLh();
            node21_it->clearPartialLh();

            // compute the score of the swapped topology
            score = optimizeOneBranch(node1, node2, false);
            if (nni_param) {
                if (cnt==0) {
                    nni_param->nni1_score = score;
                    nni_param->nni1_brlen = node12_it->length;
                } else {
                    nni_param->nni2_score = score;
                    nni_param->nni2_brlen = node12_it->length;
                }
            }
        }
        double *pattern_lh = NULL;
        /*if (treels) {
            if (!duplicated_tree) {
                (*treels)[tree_str] = treels_ptnlh->size();
                pattern_lh = new double[aln->getNPattern()];
                computePatternLikelihood(pattern_lh);
                treels_ptnlh->push_back(pattern_lh);
                treels_logl->push_back(score);
                //}
                //cout << tree_str << endl;
            }
        } else if (out_lh) {
            pattern_lh = new double[aln->getNPattern()];
            computePatternLikelihood(pattern_lh);
        }
        if (out && !duplicated_tree) printTree(*out, brtype);
        if (out_lh && !duplicated_tree) {
            (*out_lh) << score;
            double prob;
            aln->multinomialProb(pattern_lh, prob);
            (*out_lh) << "\t" << prob << endl;
            (*site_lh) << "Site_Lh   ";
            for (int i = 0; i < aln->getNSite(); i++)
                (*site_lh) << "\t" << pattern_lh[aln->getPatternID(i)];
            (*site_lh) << endl;
            if (!treels) delete [] pattern_lh;
        }*/
        // if better: return
        if (score > cur_score) {
            node2->clearReversePartialLh(node1);
            node1->clearReversePartialLh(node2);
            cur_score = score;
            cout << "Swapped neighbors :" << node1_nei->node->id << " and " << node2_nei->node->id << endl;
            break;

        }
        //cout << "Could not find better score for NNI with Node " << node1->id << "->" << "Node " << node2->id << endl;

        // else, swap back, also recover the branch lengths
        node1->updateNeighbor(node1_it, node1_nei, node1_len);
        node1_nei->node->updateNeighbor(node2, node1, node1_len);
        node2->updateNeighbor(node2_it, node2_nei, node2_len);
        node2_nei->node->updateNeighbor(node1, node2, node2_len);
        node12_it->length = node12_len;
        node21_it->length = node12_len;
    }

    // restore the partial likelihood vector
    delete [] node21_it->scale_num;
    delete [] node12_it->scale_num;
    delete [] node21_it->partial_lh;
    delete [] node12_it->partial_lh;
    node12_it->partial_lh = node1_lh_save;
    node21_it->partial_lh = node2_lh_save;
    node12_it->scale_num = node1_scale_save;
    node21_it->scale_num = node2_scale_save;
    node12_it->lh_scale_factor = node1_lh_scale;
    node21_it->lh_scale_factor = node2_lh_scale;
    return cur_score;
}

double PhyloTree::optimizeNNI(double cur_score, PhyloNode *node, PhyloNode *dad/*, ostream *out,
                              int brtype, ostream *out_lh, ostream *site_lh, StringIntMap *treels,
                              vector<double*> *treels_ptnlh, DoubleVector *treels_logl, int *max_trees, double *logl_cutoff*/)
{
    if (!node) node = (PhyloNode*) root;
    if (!node->isLeaf() && dad && !dad->isLeaf()) {
        double score = swapNNIBranch(cur_score, node, dad/*, out, brtype, NULL, out_lh, site_lh,
                                     treels, treels_ptnlh, treels_logl, max_trees, logl_cutoff*/);
        if (score > cur_score) return score;
    }

    FOR_NEIGHBOR_IT(node, dad, it) {
        double score = optimizeNNI(cur_score, (PhyloNode*) (*it)->node, node/*, out, brtype,
                                   out_lh, site_lh, treels, treels_ptnlh, treels_logl, max_trees, logl_cutoff*/);
        if (score > cur_score) return score;
    }
    return cur_score;
}

double PhyloTree::optimizeNNI() {
    double cur_score = computeLikelihood();
    for (int i = 0; i < 100; i++) {
        double score = optimizeNNI(cur_score);
        if (score <= cur_score) break;
        if (verbose_mode > VB_MED)
            cout << "NNI " << i + 1 << " : " << score << endl;
        //cur_score = score;
        cur_score = optimizeAllBranches((PhyloNode*) root);
    }
    return optimizeAllBranches();
}

double PhyloTree::optimizeNNIBranches() {
    if (verbose_mode >= VB_MED)
        cout << "Search with Nearest Neighbor Interchange (NNI) using ML..." << endl;
    double cur_score = computeLikelihood();
    for (int i = 0; i < 100; i++) {
        double score = optimizeNNI();
        if (score <= cur_score + TOL_LIKELIHOOD) break;
        cur_score = score;
    }
    return cur_score;
}

/****************************************************************************
        Subtree Pruning and Regrafting by maximum likelihood
 ****************************************************************************/

double PhyloTree::optimizeSPR_old(double cur_score, PhyloNode *node, PhyloNode *dad) {
    if (!node) node = (PhyloNode*) root;
    PhyloNeighbor *dad1_nei = NULL;
    PhyloNeighbor *dad2_nei = NULL;
    PhyloNode *sibling1 = NULL;
    PhyloNode *sibling2 = NULL;
    double sibling1_len = 0.0, sibling2_len = 0.0;

    if (dad && !dad->isLeaf()) {
        assert(dad->degree() == 3);
        // assign the sibling of node, with respect to dad

        FOR_NEIGHBOR_DECLARE(dad, node, it) {
            if (!sibling1) {
                dad1_nei = (PhyloNeighbor*) (*it);
                sibling1 = (PhyloNode*) (*it)->node;
                sibling1_len = (*it)->length;
            } else {
                dad2_nei = (PhyloNeighbor*) (*it);
                sibling2 = (PhyloNode*) (*it)->node;
                sibling2_len = (*it)->length;
            }
        }
        // remove the subtree leading to node
        double sum_len = sibling1_len + sibling2_len;
        sibling1->updateNeighbor(dad, sibling2, sum_len);
        sibling2->updateNeighbor(dad, sibling1, sum_len);
        PhyloNeighbor* sibling1_nei = (PhyloNeighbor*) sibling1->findNeighbor(sibling2);
        PhyloNeighbor* sibling2_nei = (PhyloNeighbor*) sibling2->findNeighbor(sibling1);
        sibling1_nei->clearPartialLh();
        sibling2_nei->clearPartialLh();

        // now try to move the subtree to somewhere else
        vector<PhyloNeighbor*> spr_path;

        FOR_NEIGHBOR(sibling1, sibling2, it) {
            spr_path.push_back(sibling1_nei);
            double score = swapSPR_old(cur_score, 1, node, dad, sibling1, sibling2, (PhyloNode*) (*it)->node, sibling1, spr_path);
            // if likelihood score improves, return
            if (score > cur_score) return score;
            spr_path.pop_back();
        }

        FOR_NEIGHBOR(sibling2, sibling1, it) {
            spr_path.push_back(sibling2_nei);
            double score = swapSPR_old(cur_score, 1, node, dad, sibling1, sibling2, (PhyloNode*) (*it)->node, sibling2, spr_path);
            // if likelihood score improves, return
            if (score > cur_score) return score;
            spr_path.pop_back();
        }
        // if likelihood does not imporve, swap back
        sibling1->updateNeighbor(sibling2, dad, sibling1_len);
        sibling2->updateNeighbor(sibling1, dad, sibling2_len);
        dad1_nei->node = sibling1;
        dad1_nei->length = sibling1_len;
        dad2_nei->node = sibling2;
        dad2_nei->length = sibling2_len;
        clearAllPartialLH();
    }

    FOR_NEIGHBOR_IT(node, dad, it) {
        double score = optimizeSPR_old(cur_score, (PhyloNode*) (*it)->node, node);
        if (score > cur_score) return score;
    }
    return cur_score;
}

/**
        move the subtree (dad1-node1) to the branch (dad2-node2)
 */
double PhyloTree::swapSPR_old(double cur_score, int cur_depth, PhyloNode *node1, PhyloNode *dad1,
                          PhyloNode *orig_node1, PhyloNode *orig_node2,
                          PhyloNode *node2, PhyloNode *dad2, vector<PhyloNeighbor*> &spr_path) {
    PhyloNeighbor *node1_nei = (PhyloNeighbor*) node1->findNeighbor(dad1);
    PhyloNeighbor *dad1_nei = (PhyloNeighbor*) dad1->findNeighbor(node1);
    double node1_dad1_len = node1_nei->length;
    PhyloNeighbor *node2_nei = (PhyloNeighbor*) node2->findNeighbor(dad2);

    if (dad2) {
        // now, connect (node1-dad1) to the branch (node2-dad2)
        bool first = true;
        PhyloNeighbor *node2_nei = (PhyloNeighbor*) node2->findNeighbor(dad2);
        PhyloNeighbor *dad2_nei = (PhyloNeighbor*) dad2->findNeighbor(node2);
        double len2 = node2_nei->length;

        FOR_NEIGHBOR_IT(dad1, node1, it) {
            if (first) {
                (*it)->node = dad2;
                (*it)->length = len2 / 2;
                dad2->updateNeighbor(node2, dad1, len2 / 2);
                first = false;
            } else {
                (*it)->node = node2;
                (*it)->length = len2 / 2;
                node2->updateNeighbor(dad2, dad1, len2 / 2);
            }
            ((PhyloNeighbor*) (*it))->clearPartialLh();
        }
        node2_nei->clearPartialLh();
        dad2_nei->clearPartialLh();
        node1_nei->clearPartialLh();
        vector<PhyloNeighbor*>::iterator it2;
        for (it2 = spr_path.begin(); it2 != spr_path.end(); it2++)
            (*it2)->clearPartialLh();
        clearAllPartialLH();
        // optimize relevant branches
        double score;

        /* testing different branch optimization */
        score = optimizeOneBranch(node1, dad1);
        //score = optimizeOneBranch(dad2, dad1);
        //score = optimizeOneBranch(node2, dad1);
        /*
        PhyloNode *cur_node = dad2;
        for (int i = spr_path.size()-1; i >= 0; i--) {
                score = optimizeOneBranch(cur_node, (PhyloNode*)spr_path[i]->node);
                cur_node = (PhyloNode*)spr_path[i]->node;
        }
         */
        //score = optimizeAllBranches(dad1);

        // if score improves, return
        if (score > cur_score) return score;
        // else, swap back
        node2->updateNeighbor(dad1, dad2, len2);
        dad2->updateNeighbor(dad1, node2, len2);
        node2_nei->clearPartialLh();
        dad2_nei->clearPartialLh();
        node1_nei->length = node1_dad1_len;
        dad1_nei->length = node1_dad1_len;

        // add to candiate SPR moves
        spr_moves.add(node1, dad1, node2, dad2, score);
    }
    if (cur_depth >= spr_radius) return cur_score;
    spr_path.push_back(node2_nei);

    FOR_NEIGHBOR_IT(node2, dad2, it) {
        double score = swapSPR(cur_score, cur_depth + 1, node1, dad1, orig_node1, orig_node2, (PhyloNode*) (*it)->node, node2, spr_path);
        if (score > cur_score) return score;
    }
    spr_path.pop_back();
    return cur_score;

}



double PhyloTree::optimizeSPR(double cur_score, PhyloNode *node, PhyloNode *dad) {
    if (!node) node = (PhyloNode*) root;
    PhyloNeighbor *dad1_nei = NULL;
    PhyloNeighbor *dad2_nei = NULL;
    PhyloNode *sibling1 = NULL;
    PhyloNode *sibling2 = NULL;
    double sibling1_len = 0.0, sibling2_len = 0.0;

    if (dad && !dad->isLeaf()) {
        assert(dad->degree() == 3);
        // assign the sibling of node, with respect to dad
        FOR_NEIGHBOR_DECLARE(dad, node, it) {
            if (!sibling1) {
                dad1_nei = (PhyloNeighbor*) (*it);
                sibling1 = (PhyloNode*) (*it)->node;
                sibling1_len = (*it)->length;
            } else {
                dad2_nei = (PhyloNeighbor*) (*it);
                sibling2 = (PhyloNode*) (*it)->node;
                sibling2_len = (*it)->length;
            }
        }
        // remove the subtree leading to node
        double sum_len = sibling1_len + sibling2_len;
        sibling1->updateNeighbor(dad, sibling2, sum_len);
        sibling2->updateNeighbor(dad, sibling1, sum_len);
        PhyloNeighbor* sibling1_nei = (PhyloNeighbor*) sibling1->findNeighbor(sibling2);
        PhyloNeighbor* sibling2_nei = (PhyloNeighbor*) sibling2->findNeighbor(sibling1);
        // save partial likelihood
        double* sibling1_partial_lh = sibling1_nei->partial_lh;
        double* sibling2_partial_lh = sibling2_nei->partial_lh;
        sibling1_nei->partial_lh = newPartialLh();
        sibling2_nei->partial_lh = newPartialLh();
        sibling1_nei->clearPartialLh();
        sibling2_nei->clearPartialLh();

		// now try to move the subtree to somewhere else
		vector<PhyloNeighbor*> spr_path;

		FOR_NEIGHBOR(sibling1, sibling2, it) {
			spr_path.push_back(sibling1_nei);
			double score = swapSPR(cur_score, 1, node, dad, sibling1, sibling2,
					(PhyloNode*) (*it)->node, sibling1, spr_path);
			// if likelihood score improves, return
			if (score > cur_score) {
				cout << "cur_score = " << cur_score << endl;
				cout << "Found new BETTER SCORE by SPR: " << score << endl;
				return score;
			}
			spr_path.pop_back();
		}
		FOR_NEIGHBOR(sibling2, sibling1, it) {
			spr_path.push_back(sibling2_nei);
			double score = swapSPR(cur_score, 1, node, dad, sibling1, sibling2,
					(PhyloNode*) (*it)->node, sibling2, spr_path);
			// if likelihood score improves, return
			if (score > cur_score) {
				cout << "cur_score = " << cur_score << endl;
				cout << "Found new BETTER SCORE by SPR: " << score << endl;
				return score;
			}
			spr_path.pop_back();
		}
		// if likelihood does not imporve, swap back
		sibling1->updateNeighbor(sibling2, dad, sibling1_len);
		sibling2->updateNeighbor(sibling1, dad, sibling2_len);
		dad1_nei->node = sibling1;
		dad1_nei->length = sibling1_len;
		dad2_nei->node = sibling2;
		dad2_nei->length = sibling2_len;
		delete [] sibling1_nei->partial_lh;
		delete [] sibling2_nei->partial_lh;
		sibling1_nei->partial_lh = sibling1_partial_lh;
		sibling2_nei->partial_lh = sibling2_partial_lh;
		//clearAllPartialLH();

	}

    FOR_NEIGHBOR_IT(node, dad, it) {
        double score = optimizeSPR(cur_score, (PhyloNode*) (*it)->node, node);
        if (score > cur_score) return score;
    }
    return cur_score;
}
/**
        move the subtree (dad1-node1) to the branch (dad2-node2)
 */
double PhyloTree::swapSPR(double cur_score, int cur_depth, PhyloNode *node1, PhyloNode *dad1,
                          PhyloNode *orig_node1, PhyloNode *orig_node2,
                          PhyloNode *node2, PhyloNode *dad2, vector<PhyloNeighbor*> &spr_path) {

    PhyloNeighbor *node1_nei = (PhyloNeighbor*) node1->findNeighbor(dad1);
    PhyloNeighbor *dad1_nei = (PhyloNeighbor*) dad1->findNeighbor(node1);
    double node1_dad1_len = node1_nei->length;
    PhyloNeighbor *node2_nei = (PhyloNeighbor*) node2->findNeighbor(dad2);
    PhyloNeighbor *dad2_nei = (PhyloNeighbor*) dad2->findNeighbor(node2);

    double* node1dad1_lh_save = node1_nei->partial_lh;
    double* dad1node1_lh_save = dad1_nei->partial_lh;
    double node1dad1_scale = node1_nei->lh_scale_factor;
    double dad1node1_scale = dad1_nei->lh_scale_factor;

    double* node2dad2_lh_save = node2_nei->partial_lh;
    double* dad2node2_lh_save = dad2_nei->partial_lh;
    double node2dad2_scale = node2_nei->lh_scale_factor;
    double dad2node_scale = dad2_nei->lh_scale_factor;

    double len2 = node2_nei->length;
    double newLen2 = sqrt(len2);

    if (dad2 && cur_depth >= SPR_DEPTH) {
        // now, connect (node1-dad1) to the branch (node2-dad2)
        bool first = true;
        //PhyloNeighbor *node2_nei = (PhyloNeighbor*) node2->findNeighbor(dad2);
        //PhyloNeighbor *dad2_nei = (PhyloNeighbor*) dad2->findNeighbor(node2);
        //double len2 = node2_nei->length;

        FOR_NEIGHBOR_IT(dad1, node1, it) {
        	// Finding new 2 neighbors for dad1 that are not node1
            if (first) {
                (*it)->node = dad2;
                //(*it)->length = len2 / 2;
                (*it)->length = newLen2;
                dad2->updateNeighbor(node2, dad1, newLen2);
                first = false;
            } else {
                (*it)->node = node2;
                (*it)->length = newLen2;
                node2->updateNeighbor(dad2, dad1, newLen2);
            }
            // clear all partial likelihood leading from
            // dad1 to the new neighbors
            ((PhyloNeighbor*) (*it))->clearPartialLh();
        }

        // clear partial likelihood from node2 to dad1
        node2_nei->clearPartialLh();
        // clear partial likelihood from dad2 to dad1
        dad2_nei->clearPartialLh();
        // clear partial likelihood from dad1 to node1
        node1_nei->clearPartialLh();

        // set new legnth as suggested by Alexis
        node1_nei->length = 0.9;
        dad1_nei->length = 0.9;

        //Save the partial likelihood from the removal point to the insertion point
        vector<PhyloNeighbor*>::iterator it2;
        vector<double*> saved_partial_lhs(spr_path.size());
        for (it2 = spr_path.begin(); it2 != spr_path.end(); it2++) {
        	saved_partial_lhs.push_back((*it2)->partial_lh);
        	(*it2)->partial_lh = newPartialLh();
        	(*it2)->clearPartialLh();
        }

        // optimize relevant branches
        double score;

        /* testing different branch optimization */
        score = optimizeOneBranch(node1, dad1);
        score = optimizeOneBranch(dad2, dad1);
        score = optimizeOneBranch(node2, dad1);
        score = optimizeOneBranch(orig_node1, orig_node2);


        /*
        PhyloNode *cur_node = dad2;
        for (int i = spr_path.size()-1; i >= 0; i--) {
                score = optimizeOneBranch(cur_node, (PhyloNode*)spr_path[i]->node);
                cur_node = (PhyloNode*)spr_path[i]->node;
        }
         */
        //score = optimizeAllBranches(dad1);

        // if score improves, return
        if (score > cur_score) {
        	cout << score << endl;
        	return score;
        }

        // else, swap back
        node2->updateNeighbor(dad1, dad2, len2);
        dad2->updateNeighbor(dad1, node2, len2);
        //node2_nei->clearPartialLh();
        //dad2_nei->clearPartialLh();
        // restore partial likelihood vectors
        node2_nei->partial_lh = node2dad2_lh_save;
        node2_nei->lh_scale_factor = node2dad2_scale;
        dad2_nei->partial_lh = dad2node2_lh_save;
        dad2_nei->lh_scale_factor = dad2node_scale;
        node2_nei->length = len2;
        dad2_nei->length = len2;
        node1_nei->length = node1_dad1_len;
        dad1_nei->length = node1_dad1_len;
        int index = 0;
        for (it2 = spr_path.begin(); it2 != spr_path.end(); it2++) {
        	delete [] (*it2)->partial_lh;
        	(*it2)->partial_lh = saved_partial_lhs.at(index);
        	(*it2)->unclearPartialLh();
        	index++;
        }

        // add to candiate SPR moves
        // Tung : why adding negative SPR move ?
        spr_moves.add(node1, dad1, node2, dad2, score);
    }
    if (cur_depth >= spr_radius) return cur_score;
    spr_path.push_back(node2_nei);

    FOR_NEIGHBOR_IT(node2, dad2, it) {
        double score = swapSPR(cur_score, cur_depth + 1, node1, dad1, orig_node1, orig_node2, (PhyloNode*) (*it)->node, node2, spr_path);
        if (score > cur_score) return score;
    }
    spr_path.pop_back();
    return cur_score;
}

double PhyloTree::assessSPRMove(double cur_score, const SPRMove &spr) {
    PhyloNode *dad = spr.prune_dad;
    PhyloNode *node = spr.prune_node;
    PhyloNode *dad2 = spr.regraft_dad;
    PhyloNode *node2 = spr.regraft_node;

    PhyloNeighbor *dad_nei1 = NULL;
    PhyloNeighbor *dad_nei2 = NULL;
    PhyloNode *sibling1 = NULL;
    PhyloNode *sibling2 = NULL;
    double sibling1_len = 0.0, sibling2_len = 0.0;

    PhyloNeighbor *node1_nei = (PhyloNeighbor*) node->findNeighbor(dad);
    PhyloNeighbor *dad1_nei = (PhyloNeighbor*) dad->findNeighbor(node);
    double node1_dad1_len = node1_nei->length;

    // assign the sibling of node, with respect to dad

    FOR_NEIGHBOR_DECLARE(dad, node, it) {
        if (!sibling1) {
            dad_nei1 = (PhyloNeighbor*) (*it);
            sibling1 = (PhyloNode*) (*it)->node;
            sibling1_len = (*it)->length;
        } else {
            dad_nei2 = (PhyloNeighbor*) (*it);
            sibling2 = (PhyloNode*) (*it)->node;
            sibling2_len = (*it)->length;
        }
    }
    // remove the subtree leading to node
    double sum_len = sibling1_len + sibling2_len;
    sibling1->updateNeighbor(dad, sibling2, sum_len);
    sibling2->updateNeighbor(dad, sibling1, sum_len);
    // now try to move the subtree to somewhere else

    bool first = true;
    PhyloNeighbor *node2_nei = (PhyloNeighbor*) node2->findNeighbor(dad2);
    //PhyloNeighbor *dad2_nei = (PhyloNeighbor*) dad2->findNeighbor(node2);
    double len2 = node2_nei->length;

    FOR_NEIGHBOR(dad, node, it) {
        if (first) {
            (*it)->node = dad2;
            (*it)->length = len2 / 2;
            dad2->updateNeighbor(node2, dad, len2 / 2);
            first = false;
        } else {
            (*it)->node = node2;
            (*it)->length = len2 / 2;
            node2->updateNeighbor(dad2, dad, len2 / 2);
        }
        ((PhyloNeighbor*) (*it))->clearPartialLh();
    }

    clearAllPartialLH();
    // optimize branches
    double score;
    score = optimizeAllBranches(dad);

    // if score improves, return
    if (score > cur_score) return score;
    // else, swap back
    node2->updateNeighbor(dad, dad2, len2);
    dad2->updateNeighbor(dad, node2, len2);

    node1_nei->length = node1_dad1_len;
    dad1_nei->length = node1_dad1_len;

    sibling1->updateNeighbor(sibling2, dad, sibling1_len);
    sibling2->updateNeighbor(sibling1, dad, sibling2_len);
    dad_nei1->node = sibling1;
    dad_nei1->length = sibling1_len;
    dad_nei2->node = sibling2;
    dad_nei2->length = sibling2_len;
    clearAllPartialLH();

    return cur_score;

}

double PhyloTree::optimizeSPR() {
    double cur_score = computeLikelihood();
    //spr_radius = leafNum / 5;
    spr_radius = 10;
    for (int i = 0; i < 100; i++) {
    	cout << "i = " << i << endl;
        spr_moves.clear();
        double score = optimizeSPR_old(cur_score, (PhyloNode*) root->neighbors[0]->node);
        clearAllPartialLH();
        // why this?
        if (score <= cur_score) {
            for (SPRMoves::iterator it = spr_moves.begin(); it != spr_moves.end(); it++) {
                //cout << (*it).score << endl;
                score = assessSPRMove(cur_score, *it);
                // if likelihood score improves, apply to SPR
                if (score > cur_score) break;
            }
            if (score <= cur_score) break;
        } else {
            cur_score = optimizeAllBranches();
            cout << "SPR " << i + 1 << " : " << cur_score << endl;
            cur_score = score;
        }
    }
    return cur_score;
    //return optimizeAllBranches();
}

double PhyloTree::optimizeSPRBranches() {
    cout << "Search with Subtree Pruning and Regrafting (SPR) using ML..." << endl;
    double cur_score = computeLikelihood();
    for (int i = 0; i < 100; i++) {
        double score = optimizeSPR();
        if (score <= cur_score + TOL_LIKELIHOOD) break;
        cur_score = score;
    }
    return cur_score;
}

void PhyloTree::pruneSubtree(PhyloNode *node, PhyloNode *dad, PruningInfo &info) {
    bool first = true;
    info.node = node;
    info.dad = dad;

    FOR_NEIGHBOR_IT(dad, node, it) {
        if (first) {
            info.dad_it_left = it;
            info.dad_nei_left = (*it);
            info.dad_lh_left = ((PhyloNeighbor*) (*it))->partial_lh;
            info.left_node = (*it)->node;
            info.left_len = (*it)->length;
            first = false;
        } else {
            info.dad_it_right = it;
            info.dad_nei_right = (*it);
            info.dad_lh_right = ((PhyloNeighbor*) (*it))->partial_lh;
            info.right_node = (*it)->node;
            info.right_len = (*it)->length;
        }
    }
    info.left_it = info.left_node->findNeighborIt(dad);
    info.right_it = info.right_node->findNeighborIt(dad);
    info.left_nei = (*info.left_it);
    info.right_nei = (*info.right_it);

    info.left_node->updateNeighbor(info.left_it, info.dad_nei_right);
    info.right_node->updateNeighbor(info.right_it, info.dad_nei_left);
    ((PhyloNeighbor*) info.dad_nei_right)->partial_lh = newPartialLh();
    ((PhyloNeighbor*) info.dad_nei_left)->partial_lh = newPartialLh();
}

void PhyloTree::regraftSubtree(PruningInfo &info,
                               PhyloNode *in_node, PhyloNode *in_dad) {
    NeighborVec::iterator in_node_it = in_node->findNeighborIt(in_dad);
    NeighborVec::iterator in_dad_it = in_dad->findNeighborIt(in_node);
    Neighbor *in_dad_nei = (*in_dad_it);
    Neighbor *in_node_nei = (*in_node_it);
    //double in_len = in_dad_nei->length;
    info.dad->updateNeighbor(info.dad_it_right, in_dad_nei);
    info.dad->updateNeighbor(info.dad_it_left, in_node_nei);
    // SOMETHING NEED TO BE DONE
    //in_dad->updateNeighbor(in_dad_it,

}

/****************************************************************************
        Approximate Likelihood Ratio Test with SH-like interpretation
 ****************************************************************************/


void PhyloTree::computeNNIPatternLh(
    double cur_lh,
    double &lh2, double *pattern_lh2,
    double &lh3, double *pattern_lh3,
    PhyloNode *node1, PhyloNode *node2) {
    assert(node1->degree() == 3 && node2->degree() == 3);

    // recompute pattern scaling factors if necessary
    PhyloNeighbor *node12_it = (PhyloNeighbor*) node1->findNeighbor(node2);
    PhyloNeighbor *node21_it = (PhyloNeighbor*) node2->findNeighbor(node1);
    NeighborVec::iterator it;
    const int IT_NUM = 6;

    NeighborVec::iterator saved_it[IT_NUM];
    int id = 0;

    FOR_NEIGHBOR(node1, node2, it) {
        saved_it[id++] = (*it)->node->findNeighborIt(node1);
    } else {
        saved_it[id++] = it;
    }

    FOR_NEIGHBOR(node2, node1, it) {
        saved_it[id++] = (*it)->node->findNeighborIt(node2);
    } else {
        saved_it[id++] = it;
    }
    assert(id == IT_NUM);


    Neighbor * saved_nei[IT_NUM];
    // save Neighbor and allocate new Neighbor pointer
    for (id = 0; id < IT_NUM; id++) {
        saved_nei[id] = (*saved_it[id]);
        *saved_it[id] = new PhyloNeighbor(saved_nei[id]->node, saved_nei[id]->length);
        ((PhyloNeighbor*) (*saved_it[id]))->partial_lh = newPartialLh();
        ((PhyloNeighbor*) (*saved_it[id]))->scale_num = newScaleNum();
    }

    // get the Neighbor again since it is replaced for saving purpose
    node12_it = (PhyloNeighbor*) node1->findNeighbor(node2);
    node21_it = (PhyloNeighbor*) node2->findNeighbor(node1);

    PhyloNeighbor *node2_lastnei = NULL;

    // save the first found neighbor of node 1 (excluding node2) in node1_it
    FOR_NEIGHBOR_DECLARE(node1, node2, node1_it) break;
    Neighbor *node1_nei = *node1_it;

    bool first = true;

    FOR_NEIGHBOR_IT(node2, node1, node2_it) {
        /* do the NNI swap */
        Neighbor *node2_nei = *node2_it;
        node1->updateNeighbor(node1_it, node2_nei);
        node2_nei->node->updateNeighbor(node2, node1);

        node2->updateNeighbor(node2_it, node1_nei);
        node1_nei->node->updateNeighbor(node1, node2);

        // re-optimize five adjacent branches
        double old_score = -INFINITY, new_score = old_score;

        // clear partial likelihood vector
        node12_it->clearPartialLh();
        node21_it->clearPartialLh();
        int i;
        for (i = 0; i < 2; i++) {

            new_score = optimizeOneBranch(node1, node2, false);

            FOR_NEIGHBOR(node1, node2, it) {
                //for (id = 0; id < IT_NUM; id++)
                //((PhyloNeighbor*)(*saved_it[id]))->clearPartialLh();
                ((PhyloNeighbor*) (*it)->node->findNeighbor(node1))->clearPartialLh();
                new_score = optimizeOneBranch(node1, (PhyloNode*) (*it)->node, false);
            }

            node21_it->clearPartialLh();

            FOR_NEIGHBOR(node2, node1, it) {
                //for (id = 0; id < IT_NUM; id++)
                //((PhyloNeighbor*)(*saved_it[id]))->clearPartialLh();
                ((PhyloNeighbor*) (*it)->node->findNeighbor(node2))->clearPartialLh();
                new_score = optimizeOneBranch(node2, (PhyloNode*) (*it)->node, false);
                node2_lastnei = (PhyloNeighbor*) (*it);
            }
            node12_it->clearPartialLh();
            if (new_score < old_score + TOL_LIKELIHOOD) break;
            old_score = new_score;
        }
        saveCurrentTree(new_score); // BQM: for new bootstrap

        //new_score = optimizeOneBranch(node1, node2, false);
        if (new_score > cur_lh + TOL_LIKELIHOOD)
            cout << "Alternative NNI shows better likelihood " << new_score << " > " << cur_lh << endl;
        double *result_lh;
        if (first) {
            result_lh = pattern_lh2;
            lh2 = new_score;
        } else {
            result_lh = pattern_lh3;
            lh3 = new_score;
        }
        old_score = new_score;
        computePatternLikelihood(result_lh);
        // swap back and recover the branch lengths
        node1->updateNeighbor(node1_it, node1_nei);
        node1_nei->node->updateNeighbor(node2, node1);
        node2->updateNeighbor(node2_it, node2_nei);
        node2_nei->node->updateNeighbor(node1, node2);
        first = false;
    }

    // restore the Neighbor*
    for (id = 0; id < IT_NUM; id++) {
        delete [] ((PhyloNeighbor*) * saved_it[id])->scale_num;
        delete [] ((PhyloNeighbor*) * saved_it[id])->partial_lh;
        delete (*saved_it[id]);
        (*saved_it[id]) = saved_nei[id];
    }

    // restore the length of 4 branches around node1, node2
    FOR_NEIGHBOR(node1, node2, it)
    (*it)->length = (*it)->node->findNeighbor(node1)->length;
    FOR_NEIGHBOR(node2, node1, it)
    (*it)->length = (*it)->node->findNeighbor(node2)->length;
}

void PhyloTree::resampleLh(double **pat_lh, double *lh_new) {
    int nsite = getAlnNSite();
    int nptn = getAlnNPattern();
    memset(lh_new, 0, sizeof (double) * 3);
    int i;
	IntVector boot_freq;
	aln->createBootstrapAlignment(boot_freq);
    for (i = 0; i < nptn; i++) {
        lh_new[0] += boot_freq[i] * pat_lh[0][i];
        lh_new[1] += boot_freq[i] * pat_lh[1][i];
        lh_new[2] += boot_freq[i] * pat_lh[2][i];
    }
}

// Implementation of testBranch follows Guindon et al. (2010)

double PhyloTree::testOneBranch(
    double best_score, double *pattern_lh, int reps, int lbp_reps,
    PhyloNode *node1, PhyloNode *node2, double &lbp_support) {
    const int NUM_NNI = 3;
    double lh[NUM_NNI];
    double *pat_lh[NUM_NNI];
    lh[0] = best_score;
    pat_lh[0] = pattern_lh;
    pat_lh[1] = new double[getAlnNPattern()];
    pat_lh[2] = new double[getAlnNPattern()];
    computeNNIPatternLh(best_score, lh[1], pat_lh[1], lh[2], pat_lh[2], node1, node2);
    double aLRT;
    if (lh[1] > lh[2])
        aLRT = (lh[0] - lh[1]);
    else
        aLRT = (lh[0] - lh[2]);

    int support = 0;

	lbp_support = 0.0;
	int times = max(reps, lbp_reps);

    for (int i = 0; i < times; i++) {
        double lh_new[NUM_NNI];
        // resampling estimated log-likelihood (RELL)
        resampleLh(pat_lh, lh_new);
		if (lh_new[0] > lh_new[1] && lh_new[0] > lh_new[2]) lbp_support += 1.0;
        double cs[NUM_NNI], cs_best, cs_2nd_best;
        cs[0] = lh_new[0] - lh[0];
        cs[1] = lh_new[1] - lh[1];
        cs[2] = lh_new[2] - lh[2];
        if (cs[0] >= cs[1] && cs[0] >= cs[2]) {
            cs_best = cs[0];
            if (cs[1] > cs[2]) cs_2nd_best = cs[1];
            else cs_2nd_best = cs[2];
        } else if (cs[1] >= cs[2]) {
            cs_best = cs[1];
            if (cs[0] > cs[2]) cs_2nd_best = cs[0];
            else cs_2nd_best = cs[2];
        } else {
            cs_best = cs[2];
            if (cs[0] > cs[1]) cs_2nd_best = cs[0];
            else cs_2nd_best = cs[1];
        }
        if (aLRT > (cs_best - cs_2nd_best) + 0.05) support++;
    }
    delete [] pat_lh[2];
    delete [] pat_lh[1];
    lbp_support /= times;
    return ((double) support) / times;
}

int PhyloTree::testAllBranches(int threshold, double best_score, double *pattern_lh,
                               int reps, int lbp_reps, PhyloNode *node, PhyloNode *dad) {
    int num_low_support = 0;
    if (!node) {
        node = (PhyloNode*) root;
        root->neighbors[0]->node->name = "";
    }
    if (dad && !node->isLeaf() && !dad->isLeaf()) {
		double lbp_support;
        int support = round(testOneBranch(best_score, pattern_lh, reps, lbp_reps, node, dad, lbp_support)*100);
        node->name = convertIntToString(support);
        if (lbp_reps) node->name += "/" + convertIntToString(round(lbp_support*100));
        if (support < threshold) num_low_support = 1;
        ((PhyloNeighbor*) node->findNeighbor(dad))->partial_pars[0] = support;
        ((PhyloNeighbor*) dad->findNeighbor(node))->partial_pars[0] = support;
    }
    FOR_NEIGHBOR_IT(node, dad, it)
    num_low_support += testAllBranches(threshold, best_score, pattern_lh, reps, lbp_reps, (PhyloNode*) (*it)->node, node);
    return num_low_support;
}

/****************************************************************************
        Collapse stable (highly supported) clades by one representative
 ****************************************************************************/

void PhyloTree::deleteLeaf(Node *leaf) {
    Node *near_node = leaf->neighbors[0]->node;
    assert(leaf->isLeaf() && near_node->degree() == 3);
    Node *node1 = NULL;
    Node *node2 = NULL;
    double sum_len = 0.0;

    FOR_NEIGHBOR_IT(near_node, leaf, it) {
        sum_len += (*it)->length;
        if (!node1)
            node1 = (*it)->node;
        else
            node2 = (*it)->node;
    }
    // make sure that the returned node1 and node2 are correct
    assert(node1 && node2);
    // update the neighbor
    node1->updateNeighbor(near_node, node2, sum_len);
    node2->updateNeighbor(near_node, node1, sum_len);
}

void PhyloTree::reinsertLeaf(Node *leaf, Node *node,
                             Node *dad) {
    bool first = true;
    Node *adjacent_node = leaf->neighbors[0]->node;
    Neighbor *nei = node->findNeighbor(dad);
    double len = nei->length;

    FOR_NEIGHBOR_IT(adjacent_node, leaf, it) {
        if (first) {
            (*it)->node = node;
            (*it)->length = len / 2;
            node->updateNeighbor(dad, adjacent_node, len / 2);
        } else {
            (*it)->node = dad;
            (*it)->length = len / 2;
            dad->updateNeighbor(node, adjacent_node, len / 2);
        }
        first = false;
    }
}

bool PhyloTree::isSupportedNode(PhyloNode* node, int min_support) {
    FOR_NEIGHBOR_IT(node, NULL, it)
    if (!(*it)->node->isLeaf())
        if (((PhyloNeighbor*) * it)->partial_pars[0] < min_support) {
            return false;
        }
    return true;
}

int PhyloTree::collapseStableClade(int min_support, NodeVector &pruned_taxa, StrVector &linked_name, double* &dist_mat) {
    NodeVector taxa;
    NodeVector::iterator tax_it;
    StrVector::iterator linked_it;
    getTaxa(taxa);
    IntVector linked_taxid;
    linked_taxid.resize(leafNum, -1);
    int num_pruned_taxa; // global num of pruned taxa
    int ntaxa = leafNum;
    do {
        num_pruned_taxa = 0;
        for (tax_it = taxa.begin(); tax_it != taxa.end(); tax_it++)
            if (linked_taxid[(*tax_it)->id] < 0) {
                Node *taxon = (*tax_it);
                PhyloNode *near_node = (PhyloNode*) taxon->neighbors[0]->node;
                Node *adj_taxon = NULL;
                FOR_NEIGHBOR_DECLARE(near_node, taxon, it)
                if ((*it)->node->isLeaf()) {
                    adj_taxon = (*it)->node;
                    break;
                }
                // if it is not a cherry
                if (!adj_taxon) continue;
                assert(linked_taxid[adj_taxon->id] < 0);
                PhyloNeighbor *near_nei = NULL;
                FOR_NEIGHBOR(near_node, taxon, it)
                if ((*it)->node != adj_taxon) {
                    near_nei = (PhyloNeighbor*) (*it);
                    break;
                }
                assert(near_nei);
                // continue if the cherry is not stable, or distance between two taxa is near ZERO
                if (!isSupportedNode((PhyloNode*) near_nei->node, min_support) && dist_mat[taxon->id * ntaxa + adj_taxon->id] > 2e-6) continue;
                // now do the taxon pruning
                Node *pruned_taxon = taxon, *stayed_taxon = adj_taxon;
                // prune the taxon that is far away
                if (adj_taxon->neighbors[0]->length > taxon->neighbors[0]->length) {
                    pruned_taxon = adj_taxon;
                    stayed_taxon = taxon;
                }
                deleteLeaf(pruned_taxon);
                linked_taxid[pruned_taxon->id] = stayed_taxon->id;
                pruned_taxa.push_back(pruned_taxon);
                linked_name.push_back(stayed_taxon->name);
                num_pruned_taxa++;
                // do not prune more than n-4 taxa
                if (pruned_taxa.size() >= ntaxa - 4) break;
            }
    } while (num_pruned_taxa && pruned_taxa.size() < ntaxa - 4);

    if (pruned_taxa.empty()) return 0;


    if (verbose_mode >= VB_MED)
        for (tax_it = pruned_taxa.begin(), linked_it = linked_name.begin(); tax_it != pruned_taxa.end(); tax_it++, linked_it++)
            cout << "Delete " << (*tax_it)->name << " from " << (*linked_it) << endl;

    // set root to the first taxon which was not deleted
    for (tax_it = taxa.begin(); tax_it != taxa.end(); tax_it++)
        if (linked_taxid[(*tax_it)->id] < 0) {
            root = (*tax_it);
            break;
        }
    // extract the sub alignment
    IntVector stayed_id;
    int i, j;
    for (i = 0; i < taxa.size(); i++)
        if (linked_taxid[i] < 0) stayed_id.push_back(i);
    assert(stayed_id.size() + pruned_taxa.size() == leafNum);
    Alignment *pruned_aln = new Alignment();
    pruned_aln->extractSubAlignment(aln, stayed_id, 2); // at least 2 informative characters
    nodeNum = leafNum = stayed_id.size();
    initializeTree();
    setAlignment(pruned_aln);

    double *pruned_dist = new double [leafNum * leafNum];
    for (i = 0; i < leafNum; i++)
        for (j = 0; j < leafNum; j++)
            pruned_dist[i * leafNum + j] = dist_mat[stayed_id[i] * ntaxa + stayed_id[j]];
    dist_mat = pruned_dist;
    return pruned_taxa.size();
}

int PhyloTree::restoreStableClade(Alignment *original_aln, NodeVector &pruned_taxa, StrVector &linked_name) {
    //int num_inserted_taxa;
    NodeVector::reverse_iterator tax_it;
    StrVector::reverse_iterator linked_it;
    tax_it = pruned_taxa.rbegin();
    linked_it = linked_name.rbegin();
    for (; tax_it != pruned_taxa.rend(); tax_it++, linked_it++) {
        //cout << "Reinsert " << (*tax_it)->name << " to " << (*linked_it) << endl;
        Node *linked_taxon = findNodeName((*linked_it));
        assert(linked_taxon);
        assert(linked_taxon->isLeaf());
        leafNum++;
        reinsertLeaf((*tax_it), linked_taxon, linked_taxon->neighbors[0]->node);
    }
    assert(leafNum == original_aln->getNSeq());
    nodeNum = leafNum;
    initializeTree();
    setAlignment(original_aln);
    root = findNodeName(aln->getSeqName(0));
    //if (verbose_mode >= VB_MED) drawTree(cout);
    return 0;
}


bool PhyloTree::checkEqualScalingFactor(double &sum_scaling, PhyloNode *node, PhyloNode *dad) {
    if (!node) node = (PhyloNode*) root;
    if (dad) {
        double scaling = ((PhyloNeighbor*) node->findNeighbor(dad))->lh_scale_factor +
                         ((PhyloNeighbor*) dad->findNeighbor(node))->lh_scale_factor;
        if (sum_scaling > 0) sum_scaling = scaling;
        if (fabs(sum_scaling - scaling) > 1e-6) {
            cout << sum_scaling << " " << scaling << endl;
            return false;
        }
    }
    FOR_NEIGHBOR_IT(node, dad, it)
    if (!checkEqualScalingFactor(sum_scaling, (PhyloNode*) (*it)->node, node)) return false;
    return true;
}

void PhyloTree::randomizeNeighbors(Node *node, Node *dad) {
    if (!node) node = root;
    FOR_NEIGHBOR_IT(node, dad, it)
    randomizeNeighbors((*it)->node, node);

    random_shuffle(node->neighbors.begin(), node->neighbors.end());
}
