/***************************************************************************
 *   Copyright (C) 2009 by BUI Quang Minh   *
 *   minh.bui@univie.ac.at   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "iqptree.h"

//TODO Only to test
int cntBranches = 0;

IQPTree::IQPTree() :
	PhyloTree() {
	k_represent = 0;
	p_delete = 0.0;
	dist_matrix = NULL;
	//bonus_values = NULL;
}

IQPTree::~IQPTree() {
	//if (bonus_values)
		//delete bonus_values;
	//bonus_values = NULL;
	if (dist_matrix)
		delete dist_matrix;
	dist_matrix = NULL;
	if (root != NULL)
		freeNode();
	root = NULL;
}

void IQPTree::setRepresentNum(int k_rep) {
	k_represent = k_rep;
}

void IQPTree::setProbDelete(double p_del) {
	p_delete = p_del;
}

void IQPTree::setIQPIterations(int iterations) {
	iqpnni_iterations = iterations;
}

void IQPTree::findRepresentLeaves(RepresentLeafSet &leaves, PhyloNode *node,
		PhyloNode *dad) {
	RepresentLeafSet::iterator last;
	RepresentLeafSet::iterator cur_it;
	int count;
	double admit_height = 1000000;

	leaves.clear();
	if (!node)
		node = (PhyloNode*) root;
	else if (node->isLeaf()) {
		/* set the depth to zero */
		node->height = 0.0;
		leaves.insert(node);
	}
	FOR_NEIGHBOR_IT(node, dad, it)
		{
			RepresentLeafSet leaves_it;
			findRepresentLeaves(leaves_it, (PhyloNode*) ((*it)->node), node);
			for (RepresentLeafSet::iterator nit = leaves_it.begin(); nit
					!= leaves_it.end(); nit++) {
				// use an alternative height computation by taking branch length into account
				//(*nit)->height += (*it)->length;
				(*nit)->height += 1.0;
				if (leaves.empty())
					leaves.insert(*nit);
				else {
					last = leaves.end();
					last--;
					if ((*last)->height >= (*nit)->height || leaves.size()
							< k_represent)
						leaves.insert(*nit);
				}
			}
		}
	if (leaves.size() <= k_represent)
		return;

	/* cut out if the leaf set contains more than k_represent element */

	for (count = 0, cur_it = leaves.begin(); cur_it != leaves.end(); cur_it++, count++) {
		if (count == k_represent)
			admit_height = (*cur_it)->height;
		if ((*cur_it)->height > admit_height)
			break;
	}

	/* first discard too far leaves */
	if (cur_it != leaves.end()) {
		leaves.erase(cur_it, leaves.end());
	}

	/* count the number of ties */
	last = leaves.end();
	last--;
	int num_ties;

	for (cur_it = last, num_ties = 0; cur_it != leaves.begin(); cur_it--, num_ties++)
		if ((*cur_it)->height != (*last)->height)
			break;
	if ((*cur_it)->height == (*last)->height)
		num_ties++;

	int num_discard = leaves.size() - k_represent;
	cur_it = last;
	for (int i = 0; i < num_ties && leaves.size() > k_represent; i++) {
		RepresentLeafSet::iterator prev_it = cur_it;
		prev_it--;
		if (((double) (rand()) / RAND_MAX) < (double) num_discard / num_ties)
			leaves.erase(cur_it);
		cur_it = prev_it;
	}

	// if still too many, cut the rest
	while (leaves.size() > k_represent) {
		last = leaves.end();
		last--;
		leaves.erase(last);
	}
}

void IQPTree::deleteLeaf(Node *leaf) {
	Node *near_node = leaf->neighbors[0]->node;
	assert(leaf->isLeaf() && near_node->degree() == 3);
	Node *node1 = NULL;
	Node *node2 = NULL;
	double sum_len = 0.0;
	FOR_NEIGHBOR_IT(near_node, leaf, it)
		{
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

void IQPTree::deleteLeaves(PhyloNodeVector &del_leaves,
		PhyloNodeVector &adjacent_nodes) {
	NodeVector taxa;
	// get the vector of taxa
	getTaxa(taxa);
	root = NULL;
	int remain_leaves = taxa.size();
	// now try to randomly delete some taxa of the probability of p_delete
	for (NodeVector::iterator it = taxa.begin(); it != taxa.end(); it++) {
		PhyloNode *taxon = (PhyloNode*) (*it);
		if (((double) (rand()) / RAND_MAX) < p_delete && remain_leaves > 3) {
			del_leaves.push_back(taxon);
			adjacent_nodes.push_back((PhyloNode*) (taxon->neighbors[0]->node));
			deleteLeaf(taxon);
			remain_leaves--;
		} else if (!root)
			root = taxon;
	}
}

int IQPTree::assessQuartet(Node *leaf0, Node *leaf1, Node *leaf2,
		Node *del_leaf) {
	assert(dist_matrix);
	int nseq = aln->getNSeq();
	double dist0 = dist_matrix[leaf0->id * nseq + del_leaf->id]
			+ dist_matrix[leaf1->id * nseq + leaf2->id];
	double dist1 = dist_matrix[leaf1->id * nseq + del_leaf->id]
			+ dist_matrix[leaf0->id * nseq + leaf2->id];
	double dist2 = dist_matrix[leaf2->id * nseq + del_leaf->id]
			+ dist_matrix[leaf0->id * nseq + leaf1->id];
	if (dist0 < dist1 && dist0 < dist2)
		return 0;
	if (dist1 < dist2)
		return 1;
	return 2;
}

void IQPTree::initializeBonus(PhyloNode *node, PhyloNode *dad) {
	if (!node) node = (PhyloNode*)root;
	if (dad) {
		((PhyloNeighbor*)(node->findNeighbor(dad)))->lh_scale_factor = 0.0;
		((PhyloNeighbor*)(dad->findNeighbor(node)))->lh_scale_factor = 0.0;
	}
	FOR_NEIGHBOR_IT(node, dad, it) {
		initializeBonus((PhyloNode*)((*it)->node), node);
	}
/*
	if (!bonus_values)
		bonus_values = new double[nodeNum];
	for (int i = 0; i < nodeNum; i++)
		bonus_values[i] = 0.0;*/
}

void IQPTree::raiseBonus(Node *node, Node *dad) {
	((PhyloNeighbor*)(node->findNeighbor(dad)))->lh_scale_factor++;
	((PhyloNeighbor*)(dad->findNeighbor(node)))->lh_scale_factor++;
	FOR_NEIGHBOR_IT(node, dad, it)
		raiseBonus((*it)->node, node);
}

double IQPTree::findBestBonus(Node *node, Node *dad) {
	double best_score;
	if (!node) node = root;
	if (!dad) {
		best_score = 0;
	} else {
		best_score = ((PhyloNeighbor*)(node->findNeighbor(dad)))->lh_scale_factor;
	}
	FOR_NEIGHBOR_IT(node, dad, it) {
		double score;
		score = findBestBonus((*it)->node, node);
		if (score > best_score) {
			best_score = score;
		}
	}
	return best_score;
}
	
void IQPTree::findBestBranch(double best_bonus, NodeVector &best_nodes, NodeVector &best_dads, Node *node, Node *dad) {
	if (!node) node = root;
	if (dad && ((PhyloNeighbor*)(node->findNeighbor(dad)))->lh_scale_factor == best_bonus) {
		best_nodes.push_back(node);
		best_dads.push_back(dad);
	}
	FOR_NEIGHBOR_IT(node, dad, it) {
		findBestBranch(best_bonus, best_nodes, best_dads, (*it)->node, node);
	}
}

void IQPTree::assessQuartets(PhyloNode *cur_root, PhyloNode *del_leaf) {
	const int MAX_DEGREE = 3;
	RepresentLeafSet leaves[MAX_DEGREE];
	int cnt = 0;

	// only work for birfucating tree
	assert(cur_root->degree() == MAX_DEGREE);

	// find the representative leaf set for three subtrees
	FOR_NEIGHBOR_IT(cur_root, NULL, it)
		{
			findRepresentLeaves(leaves[cnt], (PhyloNode*) ((*it)->node),
					cur_root);
			cnt++;
		}
	for (RepresentLeafSet::iterator i0 = leaves[0].begin(); i0
			!= leaves[0].end(); i0++)
		for (RepresentLeafSet::iterator i1 = leaves[1].begin(); i1
				!= leaves[1].end(); i1++)
			for (RepresentLeafSet::iterator i2 = leaves[2].begin(); i2
					!= leaves[2].end(); i2++) {
				int best_id = assessQuartet((*i0), (*i1), (*i2), del_leaf);
				raiseBonus(cur_root->neighbors[best_id]->node, cur_root);
			}

}

void IQPTree::reinsertLeaf(Node *leaf, Node *adjacent_node, Node *node,
		Node *dad) {
	bool first = true;
	Neighbor *nei = node->findNeighbor(dad);
	double len = nei->length;
	FOR_NEIGHBOR_IT(adjacent_node, leaf, it)
		{
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

void IQPTree::reinsertLeaves(PhyloNodeVector &del_leaves,
		PhyloNodeVector &adjacent_nodes) {
	PhyloNodeVector::iterator it_leaf, it_node;

	for (it_leaf = del_leaves.begin(), it_node = adjacent_nodes.begin(); it_leaf
			!= del_leaves.end(); it_leaf++, it_node++) {
		if (verbose_mode >= VB_DEBUG)
			cout << "Reinserting " << (*it_leaf)->name << endl;
		initializeBonus();
		NodeVector nodes;
		getInternalNodes(nodes);
		for (NodeVector::iterator it = nodes.begin(); it != nodes.end(); it++) {
			assessQuartets((PhyloNode*) (*it), (*it_leaf));
		}
		NodeVector best_nodes, best_dads;
		double best_bonus = findBestBonus();
		findBestBranch(best_bonus, best_nodes, best_dads);
		assert(best_nodes.size() == best_dads.size());
		int node_id = floor((((double)rand())/RAND_MAX) * best_nodes.size());
		if (best_nodes.size() > 1 && verbose_mode >= VB_DEBUG)
			cout << best_nodes.size() << " branches show the same best bonus, branch nr. " << node_id << " is chosen" << endl;

		reinsertLeaf(*it_leaf, *it_node, best_nodes[node_id], best_dads[node_id]);
		if (verbose_mode >= VB_DEBUG) {
			printTree(cout);
			cout << endl;
		}
	}
}

double IQPTree::doIQP() {

	PhyloNodeVector del_leaves, adjacent_nodes;
	deleteLeaves(del_leaves, adjacent_nodes);
	reinsertLeaves(del_leaves, adjacent_nodes);

	// just to make sure IQP does it right
	setAlignment(aln);

	clearAllPartialLh();
	//TODO Original is to optimizeAllBranches
	double tree_lh = optimizeAllBranches();
	
	if (verbose_mode >= VB_MAX) {
		cout << "IQP Likelihood = " << tree_lh << endl;
		printTree(cout);
		cout << endl;
	}
	return optimizeNNI();
}

double IQPTree::doIQPNNI(string tree_file_name) {
	double best_score = computeLikelihood();
	printTree(tree_file_name.c_str());

	// keep the best tree into a string
	stringstream best_tree_string;
	printTree(best_tree_string, WT_TAXON_ID + WT_BR_LEN);

	for (int i = 1; i <= iqpnni_iterations; i++) {
		if (verbose_mode >= VB_DEBUG)
			cout << "Performing IQP in iteration " << i << endl;
		double cur_score = doIQP();
		cout.precision(10);
		cout << "Iteration " << i + 1 << " / Log-Likelihood: " << cur_score
				<< endl;
		if (cur_score > best_score+1e-6) {
			cout << "BETTER TREE FOUND: " << cur_score << endl;
			best_score = cur_score;
			best_tree_string.seekp(0);
			printTree(best_tree_string, WT_TAXON_ID + WT_BR_LEN);
			printTree(tree_file_name.c_str());
		} else {
			/* take back the current best tree */
			best_tree_string.seekg(0);
			readTree(best_tree_string, rooted);
			assignLeafNames();
		}
	}
	return best_score;
}

/****************************************************************************
 Fast Nearest Neighbor Interchange by maximum likelihood
 ****************************************************************************/

double IQPTree::optimizeNNI() {

	clock_t nniBeginClock, nniEndClock;
	nniBeginClock = clock();

	// Switch to run "slow NNI"
	if (simple_nni) {
		return PhyloTree::optimizeNNI();
	}

	int cnt = 1;
	int nniIteration = 0;

	// Delete debug files (NNI trees)
	if (verbose_mode > VB_MED) {
		if (fileExists("nniTrees")) {
			if (remove("nniTrees") != 0)
				perror("Error deleting file nniTrees");
			else
				puts("File successfully deleted");
		}

		if ( fileExists("nniScores") ) {
			if (remove("nniScores") != 0)
				perror("Error deleting file nniScores");
			else
				puts("File successfully deleted");
		}
	}

	// Set default value of lamda to 0.75
	lamda = 0.75;
	bool resetLamda = false;
	int numbNNI = 0;

	do {

		//Backup the tree before any modification
		//TODO save the tree on memory instead of file
		IQPTree *backupTree = new IQPTree();
		backupTree->copyPhyloTree(this);

		nonConflictMoves.clear();
		mapOptBranLens.clear();
		double cur_score = computeLikelihood();

		if (resetLamda) {
			lamda = 0.75;
		}
		else {
			// TODO For debug only (delete after that)
			if (verbose_mode >= VB_DEBUG)
				cout << "Tree topology reverted, current score : " << cur_score << endl;
		}

		double old_score = cur_score;
		possibleNNIMoves.clear();
		nniIteration++;

		generateAllPositiveNNIMoves();

		if (possibleNNIMoves.size() == 0) {
			if (verbose_mode >= VB_DEBUG) {
				cout << "Could not find any improving NNIs for NNI Iteration "
						<< nniIteration << endl;
				cout << "Stop optimizing using NNI" << endl;
			}
			break;
		}

		// Sort all the possible moves (this is descending)
		sort(possibleNNIMoves.begin(), possibleNNIMoves.end());

		// Remove conflicting NNIs
		for (vector<NNIMove>::iterator iterMove = possibleNNIMoves.begin(); iterMove
				!= possibleNNIMoves.end(); iterMove++) {
			bool choosen = true;
			for (vector<NNIMove>::iterator iterNextMove =
					nonConflictMoves.begin(); iterNextMove
					!= nonConflictMoves.end(); iterNextMove++) {
				if ((*iterMove).node1 == (*(iterNextMove)).node1
						|| (*iterMove).node2 == (*(iterNextMove)).node1
						|| (*iterMove).node1 == (*(iterNextMove)).node2
						|| (*iterMove).node2 == (*(iterNextMove)).node2) {
					choosen = false;
					break;
				}
			}

			if (choosen) {
				nonConflictMoves.push_back(*iterMove);
			}

		}

		int nniTotal = nonConflictMoves.size();

		if (nniTotal == 0) {
			break;
		}

		if (verbose_mode == VB_DEBUG) {
			cout << "Number of non-conflicting NNIs found = " << nniTotal << endl;
		}

		possibleNNIMoves = nonConflictMoves;
		nbNNIToApply = (int) nonConflictMoves.size() * lamda;
		numbNNI += nbNNIToApply;

		/**
		 * Print out the number of NNIs to apply and their scores (sorted)
		 */
		if (verbose_mode == VB_DEBUG) {
			ofstream nniScore("nniScores", ios::app);
			nniScore << nbNNIToApply << endl;
			nniScore.precision(10);
			int i;
			vector<NNIMove>::iterator iterMove;
			for (iterMove = possibleNNIMoves.begin(), i = 0; iterMove
					!= possibleNNIMoves.end() && i < nbNNIToApply; iterMove++, i++) {
				nniScore << (*iterMove).score;
				nniScore << endl;
			}
			nniScore.close();
		}

		if (verbose_mode == VB_DEBUG) {
			cout << "Lamda = " << lamda << endl;
		}

		if (nbNNIToApply < 1) {
			nbNNIToApply = 1;
			lamda = 0;
		}

		if (verbose_mode == VB_DEBUG) {
			ofstream nniTreesFile("nniTrees", ios::app);
			nniTreesFile << nbNNIToApply << endl;
			nniTreesFile.close();
		}

		/**
		 Applying all non-conflicting NNIs
		 */
		for (int i = 0; i < nbNNIToApply; i++) {
			if (verbose_mode == VB_DEBUG) {
				cout << " \tApplying NNI for branch "
						<< nonConflictMoves.at(i).node1->id << "->"
						<< nonConflictMoves.at(i).node2->id << endl;
			}

//			cur_score = PhyloTree::swapNNIBranch(cur_score,nonConflictMoves.at(i).node1, nonConflictMoves.at(i).node2);
//			cout << "cur_score = " << cur_score << endl;

			// Apply the calculated optimal branch length for the center branch
			double new_len = applyBranchLengthChange(nonConflictMoves.at(i).node1,
					nonConflictMoves.at(i).node2, false);

			swapNNIBranch(nonConflictMoves.at(i));

			//Print the tree
			if (verbose_mode == VB_DEBUG) {
				// Print out the tree and its score after this NNI operation
				ofstream nniTreesFile("nniTrees", ios::app);
				nniTreesFile.precision(10);
				nniTreesFile << cur_score;
				nniTreesFile << "\t";
				printTree(nniTreesFile);
				nniTreesFile << "\n";
				nniTreesFile.close();
			}
		}

		cnt++;

		double newScore;

		if (phyml_opt) {
			applyAllBranchLengthChanges((PhyloNode*)root);
			double newScore = computeLikelihood();
		}
		else {
			// Optimize all branch lengths after each NNI-Iteration
			// This is done by IQPNNI
			newScore = optimizeAllBranches(1);
		}


		if (newScore < old_score) {
			cout << "Old score = " << old_score << endl;
			cout << "New score after applying NNIs = " << newScore << endl;
			lamda = lamda / 2;
			cout << "!!! The tree didn't improve at NNI iteration "
					<< nniIteration << " (applied NNIs=" << nbNNIToApply
					<< ") , lamda will be devided by 2 -> new lamda = "
					<< lamda << endl;
			this->copyPhyloTree(backupTree);
			resetLamda = false;
			numbNNI -= nbNNIToApply;
		} else {
			resetLamda = true;
			if (verbose_mode >= VB_DEBUG)
			cout << "New best tree found with score " << newScore
						<< " with " << nbNNIToApply << " NNIs"
						<< " -- improvement general " << (newScore - old_score)
						<< " and improvement pro NNI "
						<< (newScore - old_score) / nbNNIToApply << endl;
		}
	} while (true);
	nniEndClock = clock();
	if (verbose_mode >= VB_DEBUG) {
		cout << "Number of NNIs applied : " << numbNNI << endl;
		printf("Time used : %8.6f seconds.\n", (double) (-nniBeginClock
				+nniEndClock) / CLOCKS_PER_SEC);
	}
	return optimizeAllBranches(1);
}

void IQPTree::applyAllBranchLengthChanges(PhyloNode *node, PhyloNode *dad) {

	applyChildBranchChanges(node, dad);

	FOR_NEIGHBOR_IT(node, dad, it)
		{
			if (!(*it)->node->isLeaf()) {
				applyAllBranchLengthChanges((PhyloNode*) (*it)->node, node);
			}

		}

}

double IQPTree::applyBranchLengthChange(PhyloNode *node1, PhyloNode *node2,
		bool nonNNIBranch) {

	current_it = (PhyloNeighbor*) node1->findNeighbor(node2);
	assert(current_it);
	current_it_back = (PhyloNeighbor*) node2->findNeighbor(node1);
	assert(current_it_back);

	double current_len = current_it->length;
	string key("");
	if (node1->id < node2->id) {
		key += convertIntToString(node1->id) + "->" + convertIntToString(
				node2->id);
	} else {
		key += convertIntToString(node2->id) + "->" + convertIntToString(
				node1->id);
	}

	double optLen = mapOptBranLens[key];
	double new_len;
	if (nonNNIBranch) {
		new_len = current_len + lamda * (optLen - current_len);
	} else {
		new_len = optLen;
	}

	current_it->length = new_len;
	current_it_back->length = new_len;

	node1->clearReversePartialLh(node2);
	node2->clearReversePartialLh(node1);

	return new_len;

}

double IQPTree::calculateOptBranchLen(PhyloNode *node1, PhyloNode *node2) {

	double negative_lh, ferror;
	current_it = (PhyloNeighbor*) node1->findNeighbor(node2);
	assert(current_it);
	current_it_back = (PhyloNeighbor*) node2->findNeighbor(node1);
	assert(current_it_back);

	double current_len = current_it->length;
	double optLength;
	if (optimize_by_newton) // Newton-Raphson method
		optLength = minimizeNewton(MIN_BRANCH_LEN, current_len, MAX_BRANCH_LEN, TOL_BRANCH_LEN, negative_lh);
	else // Brent method
		optLength = minimizeOneDimen(MIN_BRANCH_LEN, current_len, MAX_BRANCH_LEN, TOL_BRANCH_LEN, &negative_lh, &ferror);

	return optLength;
}

void IQPTree::applyChildBranchChanges(PhyloNode *node, PhyloNode *dad) {

	FOR_NEIGHBOR_IT(node, dad, it)
		{
			bool branchUsed = false;
			for (int i = 0; i < nbNNIToApply; i++) {
				if ((node->id == nonConflictMoves.at(i).node1->id
						&& (*it)->node->id == nonConflictMoves.at(i).node2->id)
						|| (node->id == nonConflictMoves.at(i).node2->id
								&& (*it)->node->id
										== nonConflictMoves.at(i).node1->id)) {
					branchUsed = true;
					break;
				}
			}

			if (branchUsed) {
				continue;
			}

			applyBranchLengthChange((PhyloNode*) node,
					(PhyloNode*) (*it)->node, true);
			cntBranches++;
		}

}

double IQPTree::swapNNIBranch(NNIMove move) {

	PhyloNode *node1 = move.node1;
	PhyloNode *node2 = move.node2;
	Neighbor *node1_nei = move.node1Nei;
	Neighbor *node2_nei = move.node2Nei;

	PhyloNeighbor *node12_it = (PhyloNeighbor*) node1->findNeighbor(node2);
	PhyloNeighbor *node21_it = (PhyloNeighbor*) node2->findNeighbor(node1);

	// save the likelihood vector at themapBranLens two ends of node1-node2
	double *node1_lh_save = node12_it->partial_lh;
	double *node2_lh_save = node21_it->partial_lh;

	// TUNG save the first found neighbor (2 Neighbor total) of node 1 (excluding node2) in node1_it
	FOR_NEIGHBOR_DECLARE(node1, node2, node1_it)
			break;

	node12_it->partial_lh = newPartialLh();
	node21_it->partial_lh = newPartialLh();

	// do the NNI swap
	node1->updateNeighbor(node1_nei->node, node2_nei);
	node2_nei->node->updateNeighbor(node2, node1);

	node2->updateNeighbor(node2_nei->node, node1_nei);
	node1_nei->node->updateNeighbor(node1, node2);

	// clear partial likelihood vector
	node12_it->clearPartialLh();
	node21_it->clearPartialLh();

	node2->clearReversePartialLh(node1);
	node1->clearReversePartialLh(node2);

	// Return likelihood score only for debugging, otherwise return 0
	//return computeLikelihood();
	return 0;
}

void IQPTree::generateAllPositiveNNIMoves(PhyloNode *node, PhyloNode *dad) {

	if (!node) {
		node = (PhyloNode*) root;
	}

	if (!node->isLeaf() && dad && !dad->isLeaf()) {
		NNIMove myMove = getBestNNIMoveForBranch(node, dad);
		if (myMove.score != 0) {
			addPossibleNNIMove(myMove);
		}
	}
	// External branch
	else if (dad) {
		//double optBranchLen = optimizeOneBranch( node, dad );
		//Branch myBranch = {node, dad, optBranchLen};
		//branches.push_back( myBranch );

		double optBranchLen = calculateOptBranchLen(node, dad);
		string key("");
		if (node->id < dad->id) {
			key += convertIntToString(node->id) + "->" + convertIntToString(
					dad->id);
		} else {
			key += convertIntToString(dad->id) + "->" + convertIntToString(
					node->id);
		}
		mapOptBranLens.insert(MapBranchLength::value_type(key, optBranchLen));
	}

	FOR_NEIGHBOR_IT(node,dad,it)
		{
			generateAllPositiveNNIMoves((PhyloNode*) (*it)->node, node);
		}

}

NNIMove IQPTree::getBestNNIMoveForBranch(PhyloNode *node1, PhyloNode *node2) {
	assert(node1->degree() == 3 && node2->degree() == 3);

	NNIMove mymove = { node1, NULL, node2, NULL, 0 };

	PhyloNeighbor *node12_it = (PhyloNeighbor*) node1->findNeighbor(node2);
	PhyloNeighbor *node21_it = (PhyloNeighbor*) node2->findNeighbor(node1);
	double node12_len[4];
	node12_len[0] = node12_it->length; // Length of branch node1-node2 before swap
	// Calculate optimal branch length for node12
	double cur_score = optimizeOneBranch(node1, node2);
	double best_score = cur_score;

	//cout << "DEBUG: new score after optimize branch " << node1->id << "->" << node2->id << " : " << cur_score << endl;
	node12_len[1] = node12_it->length; // Optimal branch length of the current branch

	// save the likelihood vector at the two ends of node1-node2
	double *node1_lh_save = node12_it->partial_lh;
	double *node2_lh_save = node21_it->partial_lh;
	node12_it->partial_lh = newPartialLh();
	node21_it->partial_lh = newPartialLh();
	// save the first found neighbor of node 1 (excluding node2) in node1_it
	FOR_NEIGHBOR_DECLARE(node1, node2, node1_it)
			break;
	Neighbor *node1_nei = *node1_it;
	//mymove.node1Nei = node1_nei;
	double node1_len = node1_nei->length;
	NeighborVec::iterator node1_nei_it = node1_nei->node->findNeighborIt(node1);

	int nniNr = 1; //
	int chosenSwap = 1;
	FOR_NEIGHBOR_IT(node2, node1, node2_it)
		{
			nniNr = nniNr + 1;
			// do the NNI swap
			Neighbor *node2_nei = *node2_it;
			//mymove.node2Nei = node2_nei;

			// TUNG unused variable ?
			NeighborVec::iterator node2_nei_it =
					node2_nei->node->findNeighborIt(node2);

			double node2_len = node2_nei->length;

			node1->updateNeighbor(node1_it, node2_nei);
			node2_nei->node->updateNeighbor(node2, node1);

			node2->updateNeighbor(node2_it, node1_nei);
			node1_nei->node->updateNeighbor(node1, node2);

			// clear partial likelihood vector
			node12_it->clearPartialLh();
			node21_it->clearPartialLh();

			// compute the score of the swapped topology
			double score = optimizeOneBranch(node1, node2);

			node12_len[nniNr] = node12_it->length;

			// If score is better, save the NNI move
			if (score > best_score) {

				best_score = score;

				chosenSwap = nniNr;

				mymove.node1Nei = node1_nei;
				mymove.node2Nei = node2_nei;
				mymove.score = best_score;
				mymove.node1 = node1;
				mymove.node2 = node2;
			}

			// swap back and recover the branch lengths
			node1->updateNeighbor(node1_it, node1_nei, node1_len);
			node1_nei->node->updateNeighbor(node2, node1, node1_len);
			node2->updateNeighbor(node2_it, node2_nei, node2_len);
			node2_nei->node->updateNeighbor(node1, node2, node2_len);
			node12_it->length = node12_len[0];
			node21_it->length = node12_len[0];
		}

	delete node21_it->partial_lh;
	delete node12_it->partial_lh;
	// restore the partial likelihood vector
	node12_it->partial_lh = node1_lh_save;
	node21_it->partial_lh = node2_lh_save;

	//Save the optimal branch length
	//Branch bestBranch = { node1, node2, node12_len[chosenSwap] };
	//branches.push_back( bestBranch );

	//mapOptBranLens.insert( MapBranchLength::value_type( node1->id + node2->id , node12_len[chosenSwap] ) );
	string key("");
	if (node1->id < node2->id) {
		key += convertIntToString(node1->id) + "->" + convertIntToString(
				node2->id);
	} else {
		key += convertIntToString(node2->id) + "->" + convertIntToString(
				node1->id);
	}

	mapOptBranLens.insert(MapBranchLength::value_type(key,
			node12_len[chosenSwap]));

	return mymove;
}

void IQPTree::addPossibleNNIMove(NNIMove myMove) {
	possibleNNIMoves.push_back(myMove);
}
