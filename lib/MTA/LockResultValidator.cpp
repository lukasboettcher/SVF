/*
 * LOCKResultValidator.cpp
 *
 *  Created on: 24/07/2021
 */

#include "Util/Options.h"
#include "SVF-FE/BasicTypes.h"
#include <string>
#include <sstream>
#include "MTA/LockResultValidator.h"

using namespace SVF;
using namespace SVFUtil;

Set<std::string> LockResultValidator::getStringArg(const SVFInstruction* inst, unsigned int arg_num)
{
    assert(SVFUtil::isCallSite(inst) && "getFirstIntArg: inst is not a callsite");
    CallSite cs = SVFUtil::getLLVMCallSite(inst);
    assert((arg_num < cs.arg_size()) && "Does not has this argument");
    const GetElementPtrInst* gepinst = SVFUtil::dyn_cast<GetElementPtrInst>(cs.getArgument(arg_num));
    const Constant* arrayinst = SVFUtil::dyn_cast<Constant>(gepinst->getOperand(0));
    const ConstantDataArray* cxtarray = SVFUtil::dyn_cast<ConstantDataArray>(arrayinst->getOperand(0));
    if (!cxtarray)
    {
        Set<std::string> strvec;
        return strvec;
    }
    const std::string vthdcxtstring = cxtarray->getAsCString().str();
    return split(vthdcxtstring, ',');
}

Set<std::string> &LockResultValidator::split(const std::string &s, char delim, Set<std::string> &elems)
{
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim))
    {
        elems.insert(item);
    }
    return elems;
}

Set<std::string> LockResultValidator::split(const std::string &s, char delim)
{
    Set<std::string> elems;
    split(s, delim, elems);
    return elems;
}

inline std::string LockResultValidator::getOutput(const char *scenario, LOCK_FLAG analysisRes)
{
    std::string ret(scenario);
    ret += "\t";
    switch (analysisRes)
    {
    case LOCK_TRUE:
        ret += SVFUtil::sucMsg("SUCCESS");
        break;
    case LOCK_UNSOUND:
        ret += SVFUtil::bugMsg2("UNSOUND");
        break;
    case LOCK_IMPRECISE:
        ret += SVFUtil::bugMsg1("IMPRECISE");
        break;
    default:
        ret += SVFUtil::errMsg("FAILURE");
    }
    return ret;
}

bool LockResultValidator::collectLockTargets()
{
    const Function* F = nullptr;
    for(auto it = getModule()->llvmFunBegin(); it != getModule()->llvmFunEnd(); it++)
    {
        const std::string fName = (*it)->getName().str();
        if(fName.find(LOCK) != std::string::npos)
        {
            F = (*it);
            break;
        }
    }
    if (!F)
        return false;
    for(Value::const_use_iterator it = F->use_begin(), ie = F->use_end(); it!=ie; it++)
    {
        const Use *u = &*it;
        const Value *user = u->getUser();
        const Instruction *inst = SVFUtil::dyn_cast<Instruction>(user);
        const SVFInstruction* svfInst = LLVMModuleSet::getLLVMModuleSet()->getSVFInstruction(inst);
        CxtLockSetStr y = getStringArg(svfInst, 0);
        const Instruction* memInst = getPreviousMemoryAccessInst(inst);
        const SVFInstruction* svfMemInst = LLVMModuleSet::getLLVMModuleSet()->getSVFInstruction(memInst);

        instToCxtLockSet[svfMemInst] = y;
        if(const StoreInst* store = SVFUtil::dyn_cast<StoreInst> (memInst))
        {
            if(const BinaryOperator* bop = SVFUtil::dyn_cast<BinaryOperator> (store->getValueOperand()))
            {
                const Value* v = bop->getOperand(0);
                const Instruction* prevInst = SVFUtil::dyn_cast<LoadInst> (v);
                const SVFInstruction* svfPrevInst = LLVMModuleSet::getLLVMModuleSet()->getSVFInstruction(prevInst);
                instToCxtLockSet[svfPrevInst] = y;
            }
        }
    }
    return true;
}

LockResultValidator::LOCK_FLAG LockResultValidator::validateStmtInLock()
{
    LockResultValidator::LOCK_FLAG res = LockResultValidator::LOCK_TRUE;
    LockAnalysis::CxtStmtToCxtLockSet analyedLS = _la->getCSTCLS();
    for(LockAnalysis::CxtStmtToCxtLockSet::iterator it = analyedLS.begin(),
            eit = analyedLS.end(); it!=eit; it++)
    {
        const SVFInstruction* inst = ((*it).first).getStmt();
        if(!SVFUtil::isa<LoadInst> (inst->getLLVMInstruction()) && !SVFUtil::isa<StoreInst> (inst->getLLVMInstruction()))
            continue;
        const SVFFunction* F = inst->getFunction();
        if(inFilter(F->getName()))
            continue;
        CxtLockSetStr LS = instToCxtLockSet[inst];
        if(LS.size() != (*it).second.size())
        {
            if (Options::PrintValidRes)
            {
                outs() << errMsg("\nValidate Stmt's Lock : Wrong at: ") << SVFUtil::value2String(inst->getLLVMInstruction()) << "\n";
                outs() << "Reason: The number of lock on current stmt is wrong\n";
                outs() << "\n----Given locks:\n";
                for (CxtLockSetStr::iterator it1 = LS.begin(),eit1 = LS.end(); it1 != eit1; it++)
                {
                    outs() << "Lock  " << *it1 << " ";
                }
                outs() << "\n----Analysis locks:\n";
                for (LockAnalysis::CxtLockSet::iterator it2 = (*it).second.begin(),
                        eit2 = (*it).second.end(); it2 != eit2; ++it)
                {
                    const SVFInstruction* call = (*it2).getStmt();
                    std::string lockName = call->getLLVMInstruction()->getOperand(0)->getName().str();
                    outs()<<"Lock  " << lockName << " ";
                }
                outs() << "\n";
            }
            res = LockResultValidator::LOCK_UNSOUND;
        }
        LockAnalysis::CxtLockSet LSA = (*it).second;

        for(LockAnalysis::CxtLockSet::iterator it3 = LSA.begin(), eit3=LSA.end(); it3!=eit3; it3++)
        {
            const SVFInstruction* call = (*it3).getStmt();
            std::string lockName = call->getLLVMInstruction()->getOperand(0)->getName().str();
            if(!match(lockName, LS))
            {
                if(Options::PrintValidRes)
                {
                    outs() << "\nValidate Stmt's Lock : Wrong at (" << SVFUtil::value2String(inst->getLLVMInstruction()) << ")\n";
                    outs() << "Reason: The number of lock on current stmt is wrong\n";
                    outs() << "\n Lock " << lockName << " should not protect current instruction\n";
                    res = LockResultValidator::LOCK_IMPRECISE;
                }
            }
        }
    }
    return res;
}

void LockResultValidator::analyze()
{
    outs() << SVFUtil::pasMsg(" --- Lock Analysis Result Validation ---\n");
    if(!collectLockTargets())
        return;
    std::string errstring;
    errstring = getOutput("Validate Lock Analysis :", validateStmtInLock());
    outs() << "======" << errstring << "======\n";
}

