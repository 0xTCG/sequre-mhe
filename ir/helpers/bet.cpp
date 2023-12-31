#include "bet.h"
#include "utils.h"

namespace sequre {

using namespace codon::ir;

BETNode::BETNode()
  : value(nullptr), irType(nullptr), operation(noop), leftChild(nullptr), rightChild(nullptr), expanded(false) {}

BETNode::BETNode( Value *value )
  : value(value), irType(nullptr), operation(noop), leftChild(nullptr), rightChild(nullptr), expanded(false) {
  getOrRealizeIRType();
}

BETNode::BETNode( Operation operation, BETNode *leftChild, BETNode *rightChild )
  : value(nullptr), irType(nullptr), operation(operation), leftChild(leftChild), rightChild(rightChild), expanded(false) {}

BETNode::BETNode( Value *value, types::Type *irType, Operation operation, bool expanded )
  : value(value), irType(irType), operation(operation), leftChild(nullptr), rightChild(nullptr), expanded(expanded) {}

BETNode *BETNode::copy() const {
  auto *newNode = new BETNode(value, irType, operation, expanded);
  auto *lc      = getLeftChild();
  auto *rc      = getRightChild();
  
  if ( lc ) newNode->setLeftChild(lc->copy());
  if ( rc ) newNode->setRightChild(rc->copy());
  
  return newNode;
}

bool BETNode::checkIsCiphertensor() {
  return isCiphertensor(getOrRealizeIRType());
}

bool BETNode::checkIsSecureContainer() {
  return isSecureContainer(getOrRealizeIRType());
}

bool BETNode::checkIsCipherCiphertensor() {
  if ( !checkIsCiphertensor() ) return false;
  return hasCKKSCiphertext(getOrRealizeIRType());
}

bool BETNode::checkIsPlainCiphertensor() {
  if ( !checkIsCiphertensor() ) return false;
  return hasCKKSPlaintext(getOrRealizeIRType());
}

bool BETNode::checkIsSameTree( BETNode *other ) const {
  if ( isLeaf() && other->isLeaf() ) {
    if ( checkIsIntConst() && other->checkIsIntConst() ) return getIntConst() == other->getIntConst();
    if ( checkIsDoubleConst() && other->checkIsDoubleConst() ) return getDoubleConst() == other->getDoubleConst();
    if ( checkIsVariable() && other->checkIsVariable() ) return getVariableId() == other->getVariableId();
  } else if ( !isLeaf() && !other->isLeaf() ) {
    if ( isOperation() && other->isOperation() && getOperation() != other->getOperation() ) return false;

    if ( getLeftChild()->checkIsSameTree(other->getLeftChild()) &&
         getRightChild()->checkIsSameTree(other->getRightChild()) ) return true;

    if ( isCommutative() )
      if ( getLeftChild()->checkIsSameTree(other->getRightChild()) &&
           getRightChild()->checkIsSameTree(other->getLeftChild()) ) return true;
  }

  return false;
}

bool BETNode::checkIsConsecutiveCommutative() const {
  if ( !isCommutative() ) return false;
  if ( getOperation() != getLeftChild()->getOperation() &&
       getOperation() != getRightChild()->getOperation() ) return false;

  return true;
}

void BETNode::replace( BETNode *other ) {
  value      = other->getValue();
  irType     = other->getOrRealizeIRType();
  operation  = other->getOperation();
  leftChild  = other->getLeftChild();
  rightChild = other->getRightChild();
  expanded   = other->isExpanded();
}

types::Type *BETNode::getOrRealizeIRType( bool force ) {
  if ( irType && !force) return irType;
  if ( isLeaf() && checkIsTypeable() ) {
    irType = value->getType();
    return irType;
  }
  if ( isLeaf() ) assert( false && "Cannot realize crypto type (leaf is not typeable)" );

  // Realize IR type from children
  auto *lc = getLeftChild();
  auto *rc = getRightChild();

  auto *lcType = lc->getOrRealizeIRType(force);
  auto *rcType = rc->getOrRealizeIRType(force);
  
  // Not possible for lc or rc to have invalid type here
  assert( lcType && "Crypto type realization error (left child type could not be realized)" );
  assert( rcType && "Crypto type realization error (left child type could not be realized)" );

  if ( lc->checkIsCipherCiphertensor() ) irType = lcType;
  else if ( rc->checkIsCipherCiphertensor() ) irType = rcType;
  else if ( lc->checkIsPlainCiphertensor() ) irType = lcType;
  else if ( rc->checkIsPlainCiphertensor() ) irType = rcType;
  else irType = lcType;

  assert( irType && "Cannot realize crypto type" );
  return irType;
}

std::string const BETNode::getOperationIRName( bool restrict = true ) const {
  if ( isAdd() ) return Module::ADD_MAGIC_NAME;
  if ( isMul() ) return Module::MUL_MAGIC_NAME;
  if ( isMatmul() ) return Module::MATMUL_MAGIC_NAME;
  if ( isPow() ) return Module::POW_MAGIC_NAME;
  if ( restrict ) assert(false && "BET node operator not supported in MHE IR optimizations.");
  else return "None";
}

std::string const BETNode::getName() const {
  if ( isOperation() ) return getOperationIRName();
  if ( checkIsVariable() ) return getVariable()->getName();
  if ( checkIsConst() )  return getConstStr();
  return "Non-parsable";
}

std::string const BETNode::getConstStr() const {
  if ( checkIsDoubleConst() ) return std::to_string(getDoubleConst());
  if ( checkIsIntConst() ) return std::to_string(getIntConst());
  return "Non-constant";
}

void BETNode::print( int level = 0, int maxLevel = 100) const {
  if ( level >= maxLevel ) return;

  for (int i = 0; i < level; ++i)
    std::cout << "    ";

  std::cout << getOperationIRName(false) << " " << ( checkIsVariable() ? getVariable()->getName() : "Non-variable" )
            << ( checkIsConst() ? " Constant " : " Non-constant " )
            << value << std::endl;

  if ( leftChild ) leftChild->print(level + 1, maxLevel);
  if ( rightChild ) rightChild->print(level + 1, maxLevel);
}

void BET::expandNode( BETNode *betNode ) {
  if ( betNode->isExpanded() ) return;

  if ( betNode->isLeaf() ) {
    assert(betNode->checkIsVariable() && "Node needs to be a variable for expansion");
    auto search = betPerVar.find(betNode->getVariableId());
    if ( search != betPerVar.end() ) betNode->replace(search->second);
  } else {
    expandNode(betNode->getLeftChild());
    expandNode(betNode->getRightChild());
  }

  betNode->setExpanded();
}

bool BET::reduceLvl( BETNode *node, bool cohort = false ) {
  if ( !cohort )
    std::cerr << "WARNING: Make sure to re-realize IR types by calling getOrRealizeIRType on node after reducing multiplications\n";
  if ( node->isLeaf() ) return false;
  if ( !node->isAdd() ) return reduceLvl(node->getLeftChild(), cohort) || reduceLvl(node->getRightChild(), cohort);

  std::vector<BETNode *> visited;
  std::unordered_map<BETNode *, std::vector<BETNode *>> metadata;
  std::pair<BETNode *, BETNode*> factors = findFactorizationNodes(node, visited, metadata);
  if ( !factors.first || !factors.second ) return false;

  BETNode *factor               = factors.first;
  BETNode *firstFactorParent    = metadata[factor][0];
  BETNode *firstFactorSibling   = metadata[factor][1];
  BETNode *firstFactorAncestor  = metadata[factor][2];
  BETNode *secondFactorParent   = metadata[factors.second][0];
  BETNode *secondFactorSibling  = metadata[factors.second][1];
  BETNode *secondFactorAncestor = metadata[factors.second][2];
  BETNode *vanisihingAdd        = metadata[factors.second][3];
  BETNode *vanisihingAddTail    = metadata[factors.second][4];

  // Delete secondFactorParent from secondFactorAncestor mul subtree
  secondFactorParent->replace(secondFactorSibling);
  
  // Delete firstFactorParent from firstFactorAncestor mul subtree
  firstFactorParent->replace(firstFactorSibling);

  // Replace firstFactorAncestor with the new subtree
  firstFactorAncestor->setRightChild(new BETNode(add, firstFactorAncestor->copy(), secondFactorAncestor));
  firstFactorAncestor->setLeftChild(factor);
  firstFactorAncestor->setOperation(mul);
  firstFactorAncestor->setValue(nullptr);
  
  // Delete the vanishingAdd node
  vanisihingAdd->replace(vanisihingAddTail);

  return true;
}

bool BET::reduceAll( BETNode *root ) {
  auto reduced = false;
  while ( reduceLvl(root, true) ) reduced = true;
  if ( reduced ) root->getOrRealizeIRType(true);
  return reduced;
}

bool BET::swapPriorities( BETNode *root, BETNode *child ) {
  if ( root->getOperation() != child->getOperation() ) return false;

  auto *lc = root->getLeftChild();
  auto *rc = root->getRightChild();
  assert ( (lc == child || rc == child) && "Invalid parameters for BET::swapPriorities (second parameter has to be child of the first parameter)" );
  
  auto *sibling = ( lc == child ? rc : lc);
  if ( sibling->checkIsSecureContainer() ) return false;

  auto *lcc = child->getLeftChild();
  auto *rcc = child->getRightChild();

  if ( lcc->checkIsSecureContainer() && rcc->checkIsSecureContainer() ) return false;

  auto *cipherGrandChild = ( lcc->checkIsSecureContainer() ? lcc : rcc );

  if ( cipherGrandChild == lcc ) child->setLeftChild(sibling);
  else child->setRightChild(sibling);

  if ( sibling == lc ) root->setLeftChild(cipherGrandChild);
  else root->setRightChild(cipherGrandChild);

  child->setIRType(nullptr);
  child->getOrRealizeIRType();
  return true;
}

bool BET::reorderPriority( BETNode *node ) {
  if ( node->isLeaf() || !node->checkIsSecureContainer() ) return false;

  auto *lc = node->getLeftChild();
  auto *rc = node->getRightChild();

  if ( node->checkIsConsecutiveCommutative() ) {
    if ( swapPriorities(node, lc) ) return true;
    if ( swapPriorities(node, rc) ) return true;
  }

  return reorderPriority(lc) || reorderPriority(rc);
}

bool BET::reorderPriorities( BETNode *root ) {
  auto reordered = false;
  while ( reorderPriority(root) ) reordered = true;
  return reordered;
}

void BET::escapePows( BETNode *node ) {
  if ( node->isLeaf() ) return;

  if ( !node->isPow() ) {
    escapePows(node->getLeftChild());
    escapePows(node->getRightChild());
    return;
  }

  auto *lc = node->getLeftChild();
  auto *rc = node->getRightChild();

  assert(rc->checkIsIntConst() &&
         "Sequre factorization optimization expects each exponent to be an integer constant.");
  assert(rc->getIntConst() > 0 &&
         "Sequre factorization optimization expects each exponent to be positive.");
  if ( rc->getIntConst() == 1 ) {
    node->replace(lc);
    return;
  }

  auto *newMulNode = new BETNode(mul, lc, lc);
  for (int i = 0; i < rc->getIntConst() - 2; ++i) newMulNode = new BETNode(mul, lc, newMulNode->copy());

  node->replace(newMulNode);
}

std::pair<BETNode *, BETNode *> BET::findFactorizationNodes(
    BETNode *node, std::vector<BETNode *> &visited, std::unordered_map<BETNode *, std::vector<BETNode *>> &metadata ) {
  assert(node->isAdd() && "BET: Tried to find factors in non-addition tree.");

  BETNode *lc = node->getLeftChild();
  BETNode *rc = node->getRightChild();

  std::pair<BETNode *, BETNode *> factors = std::make_pair(nullptr, nullptr);
  if ( lc->isMul() ) {
    factors = findFactorsInMulTree(lc, visited, metadata, lc, node, rc);
    if ( factors.first and factors.second ) return factors;
  }
  else if ( lc->isAdd() ) {
    factors = findFactorizationNodes(lc, visited, metadata);
    if ( factors.first and factors.second ) return factors;
  }
  if ( rc->isMul() ) {
    factors = findFactorsInMulTree(rc, visited, metadata, rc, node, lc);
    if ( factors.first and factors.second ) return factors;
  }
  else if ( rc->isAdd() ) {
    factors = findFactorizationNodes(rc, visited, metadata);
    if ( factors.first and factors.second ) return factors;
  }

  return factors;
}

BETNode *BET::internalIsVisited( BETNode *node, std::vector<BETNode *> &visited, std::unordered_map<BETNode *, std::vector<BETNode *>> &metadata, BETNode *firstMulAncestor ) {
  for (BETNode *n : visited) {
    if ( metadata[n][2] == firstMulAncestor ) continue;
    if ( node->checkIsSameTree(n) ) return n;
  }
  return nullptr;
}

std::pair<BETNode *, BETNode *> BET::findFactorsInMulTree(
    BETNode *node, std::vector<BETNode *> &visited,
    std::unordered_map<BETNode *, std::vector<BETNode *>> &metadata,
    BETNode *firstMulAncestor, BETNode *addAncestor, BETNode *addTail ) {
  assert(node->isMul() && "BET: Tried to find factors in non-multiplication tree.");

  BETNode *lc = node->getLeftChild();
  BETNode *rc = node->getRightChild();
  
  std::pair<BETNode *, BETNode *> factors = std::make_pair(nullptr, nullptr);

  if ( !lc->isMul() ) {
    metadata[lc].push_back(node);
    metadata[lc].push_back(rc);
    metadata[lc].push_back(firstMulAncestor);
    metadata[lc].push_back(addAncestor);
    metadata[lc].push_back(addTail);

    if ( (factors.second = internalIsVisited(lc, visited, metadata, firstMulAncestor)) ) {
      factors.first = lc;
      return factors;
    } else visited.push_back(lc);
  }

  if ( !rc->isMul() ) {
    metadata[rc].push_back(node);
    metadata[rc].push_back(lc);
    metadata[rc].push_back(firstMulAncestor);
    metadata[rc].push_back(addAncestor);
    metadata[rc].push_back(addTail);

    if ( (factors.second = internalIsVisited(rc, visited, metadata, firstMulAncestor)) ) {
      factors.first = rc;
      return factors;
    } else visited.push_back(rc);
  }
  
  if ( lc->isMul() ) factors = findFactorsInMulTree(lc, visited, metadata, firstMulAncestor, addAncestor, addTail);
  if ( factors.first ) return factors;

  if ( rc->isMul() ) factors = findFactorsInMulTree(rc, visited, metadata, firstMulAncestor, addAncestor, addTail);
  return factors;
}

BETNode *parseArithmetic( CallInstr *callInstr ) {
  Operation operation = getOperation(callInstr);
  auto *betNode       = new BETNode();
  
  betNode->setValue(callInstr);
  betNode->setIRType(callInstr->getType());
  betNode->setOperation(operation);
  
  if ( !isArithmeticOperation(operation) ) {
    betNode->setExpanded();
    return betNode;
  }

  auto *lhs = callInstr->front();
  auto *rhs = callInstr->back();
  
  auto *lhsInstr = cast<CallInstr>(lhs);
  auto *rhsInstr = cast<CallInstr>(rhs);

  if ( lhsInstr ) betNode->setLeftChild(parseArithmetic(lhsInstr));
  else betNode->setLeftChild(new BETNode(lhs));

  if ( rhsInstr ) betNode->setRightChild(parseArithmetic(rhsInstr));
  else betNode->setRightChild(new BETNode(rhs));

  return betNode;
}

Value *generateExpression( Module *M, BETNode *node ) {
  if ( node->isLeaf() )
    return node->getVarValue();

  auto *lc = node->getLeftChild();
  auto *rc = node->getRightChild();
  assert(lc);
  assert(rc);

  auto *lopType = lc->getOrRealizeIRType();
  auto *ropType = rc->getOrRealizeIRType();

  auto *opFunc = M->getOrRealizeMethod(
    lopType, node->getOperationIRName(), {lopType, ropType});

  if ( !opFunc ) {
    std::cout << "ERROR: "
              << node->getOperationIRName()
              << " not found in type "
              << lopType->getName()
              << " with arguments ("
              << lopType->getName()
              << ", "
              << ropType->getName()
              << ")"
              << std::endl;
    assert(false);
  }

  auto *lop = generateExpression(M, lc);
  assert(lop);
  auto *rop = generateExpression(M, rc);
  assert(rop);

  auto *callIns       = util::call(opFunc, {lop, rop});
  assert(callIns);
  auto *actualCallIns = callIns->getActual();
  assert(actualCallIns);

  return actualCallIns;
}

} // namespace sequre