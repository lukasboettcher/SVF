//===- SymbolTableBuilder.cpp -- Symbol Table builder---------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013->  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//

/*
 * SymbolTableBuilder.cpp
 *
 *  Created on: Apr 28, 2014
 *      Author: Yulei Sui
 */

#include <memory>

#include "SVF-FE/SymbolTableBuilder.h"
#include "Util/NodeIDAllocator.h"
#include "Util/Options.h"
#include "Util/SVFModule.h"
#include "Util/SVFUtil.h"
#include "SVF-FE/BasicTypes.h"
#include "SVF-FE/LLVMUtil.h"
#include "SVF-FE/CPPUtil.h"
#include "SVF-FE/GEPTypeBridgeIterator.h" // include bridge_gep_iterator

using namespace SVF;
using namespace SVFUtil;
using namespace LLVMUtil;

/*!
 *  This method identify which is value sym and which is object sym
 */
void SymbolTableBuilder::buildMemModel(SVFModule* svfModule)
{
    SVFUtil::increaseStackSize();

    symInfo->setModule(svfModule);

    // Pointer #0 always represents the null pointer.
    assert(symInfo->totalSymNum++ == SymbolTableInfo::NullPtr && "Something changed!");

    // Pointer #1 always represents the pointer points-to black hole.
    assert(symInfo->totalSymNum++ == SymbolTableInfo::BlkPtr && "Something changed!");

    // Object #2 is black hole the object that may point to any object
    assert(symInfo->totalSymNum++ == SymbolTableInfo::BlackHole && "Something changed!");
    symInfo->createBlkObj(SymbolTableInfo::BlackHole);

    // Object #3 always represents the unique constant of a program (merging all constants if Options::ModelConsts is disabled)
    assert(symInfo->totalSymNum++ == SymbolTableInfo::ConstantObj && "Something changed!");
    symInfo->createConstantObj(SymbolTableInfo::ConstantObj);

    for (Module &M : LLVMModuleSet::getLLVMModuleSet()->getLLVMModules())
    {
        // Add symbols for all the globals .
        for (Module::global_iterator I = M.global_begin(), E = M.global_end(); I != E; ++I)
        {
            collectSym(&*I);
        }

        // Add symbols for all the global aliases
        for (Module::alias_iterator I = M.alias_begin(), E = M.alias_end(); I != E; I++)
        {
            collectSym(&*I);
            collectSym((&*I)->getAliasee());
        }

        // Add symbols for all of the functions and the instructions in them.
        for (Module::const_iterator F = M.begin(), E = M.end(); F != E; ++F)
        {
            const Function *fun = &*F;
            collectSym(fun);
            collectRet(fun);
            if (fun->getFunctionType()->isVarArg())
                collectVararg(fun);

            // Add symbols for all formal parameters.
            for (Function::const_arg_iterator I = fun->arg_begin(), E = fun->arg_end();
                    I != E; ++I)
            {
                collectSym(&*I);
            }

            // collect and create symbols inside the function body
            for (const_inst_iterator II = inst_begin(*fun), E = inst_end(*fun); II != E; ++II)
            {
                const Instruction* inst = &*II;
                collectSym(inst);

                // initialization for some special instructions
                //{@
                if (const StoreInst *st = SVFUtil::dyn_cast<StoreInst>(inst))
                {
                    collectSym(st->getPointerOperand());
                    collectSym(st->getValueOperand());
                }
                else if (const LoadInst *ld = SVFUtil::dyn_cast<LoadInst>(inst))
                {
                    collectSym(ld->getPointerOperand());
                }
                else if (const AllocaInst *alloc = SVFUtil::dyn_cast<AllocaInst>(inst))
                {
                    collectSym(alloc->getArraySize());
                }
                else if (const PHINode *phi = SVFUtil::dyn_cast<PHINode>(inst))
                {
                    for (u32_t i = 0; i < phi->getNumIncomingValues(); ++i)
                    {
                        collectSym(phi->getIncomingValue(i));
                    }
                }
                else if (const GetElementPtrInst *gep = SVFUtil::dyn_cast<GetElementPtrInst>(
                        inst))
                {
                    collectSym(gep->getPointerOperand());
                }
                else if (const SelectInst *sel = SVFUtil::dyn_cast<SelectInst>(inst))
                {
                    collectSym(sel->getTrueValue());
                    collectSym(sel->getFalseValue());
                    collectSym(sel->getCondition());
                }
                else if (const BinaryOperator *binary = SVFUtil::dyn_cast<BinaryOperator>(inst))
                {
                    for (u32_t i = 0; i < binary->getNumOperands(); i++)
                        collectSym(binary->getOperand(i));
                }
                else if (const UnaryOperator *unary = SVFUtil::dyn_cast<UnaryOperator>(inst))
                {
                    for (u32_t i = 0; i < unary->getNumOperands(); i++)
                        collectSym(unary->getOperand(i));
                }
                else if (const CmpInst *cmp = SVFUtil::dyn_cast<CmpInst>(inst))
                {
                    for (u32_t i = 0; i < cmp->getNumOperands(); i++)
                        collectSym(cmp->getOperand(i));
                }
                else if (const CastInst *cast = SVFUtil::dyn_cast<CastInst>(inst))
                {
                    collectSym(cast->getOperand(0));
                }
                else if (const ReturnInst *ret = SVFUtil::dyn_cast<ReturnInst>(inst))
                {
                    if(ret->getReturnValue())
                        collectSym(ret->getReturnValue());
                }
                else if (const BranchInst *br = SVFUtil::dyn_cast<BranchInst>(inst))
                {
                    Value* opnd = br->isConditional() ? br->getCondition() : br->getOperand(0);
                    collectSym(opnd);
                }
                else if (const SwitchInst *sw = SVFUtil::dyn_cast<SwitchInst>(inst))
                {
                    collectSym(sw->getCondition());
                }
                else if (isNonInstricCallSite(LLVMModuleSet::getLLVMModuleSet()->getSVFInstruction(inst)))
                {

                    const CallBase* cs = LLVMUtil::getLLVMCallSite(inst);
                    for (u32_t i = 0; i < cs->arg_size(); i++)
                    {
                        collectSym(cs->getArgOperand(i));
                    }
                    // Calls to inline asm need to be added as well because the callee isn't
                    // referenced anywhere else.
                    const Value *Callee = cs->getCalledOperand();
                    collectSym(Callee);

                    //TODO handle inlineAsm
                    ///if (SVFUtil::isa<InlineAsm>(Callee))

                }
                //@}
            }
        }
    }

    symInfo->totalSymNum = NodeIDAllocator::get()->endSymbolAllocation();
    if (Options::SymTabPrint)
    {
        symInfo->dump();
    }
}

void SymbolTableBuilder::collectSVFTypeInfo(const Value* val)
{    
    (void)getStructInfo(val->getType());
    if (const PointerType * ptrType = SVFUtil::dyn_cast<PointerType>(val->getType()))
    {
        const Type* objtype = LLVMUtil::getPtrElementType(ptrType);
        symInfo->getModule()->addptrElementType(ptrType, objtype);
        (void)getStructInfo(objtype);
    }
    if(isGepConstantExpr(val) || SVFUtil::isa<GetElementPtrInst>(val)){
            for (bridge_gep_iterator gi = bridge_gep_begin(SVFUtil::cast<User>(val)), ge = bridge_gep_end(SVFUtil::cast<User>(val));
            gi != ge; ++gi)
        {
            const Type* gepTy = *gi;
            (void)getStructInfo(gepTy);
        }
    }
}

/*!
 * Collect symbols, including value and object syms
 */
void SymbolTableBuilder::collectSym(const Value *val)
{

    //TODO: filter the non-pointer type // if (!SVFUtil::isa<PointerType>(val->getType()))  return;

    DBOUT(DMemModel, outs() << "collect sym from ##" << SVFUtil::value2String(LLVMModuleSet::getLLVMModuleSet()->getSVFValue(val)) << " \n");

    //TODO handle constant expression value here??
    handleCE(val);

    // create a value sym
    collectVal(val);

    collectSVFTypeInfo(val);
    collectSVFTypeInfo(LLVMUtil::getGlobalRep(val));

    // create an object If it is a heap, stack, global, function.
    if (isObject(val))
    {
        collectObj(val);
    }
}

/*!
 * Get value sym, if not available create a new one
 */
void SymbolTableBuilder::collectVal(const Value *val)
{
    // collect and record special sym here
    if (LLVMUtil::isNullPtrSym(val) || LLVMUtil::isBlackholeSym(val))
    {
        return;
    }
    SymbolTableInfo::ValueToIDMapTy::iterator iter = symInfo->valSymMap.find(LLVMModuleSet::getLLVMModuleSet()->getSVFValue(val));
    if (iter == symInfo->valSymMap.end())
    {
        // create val sym and sym type
        SVFValue* svfVal = LLVMModuleSet::getLLVMModuleSet()->getSVFValue(val);
        SymID id = NodeIDAllocator::get()->allocateValueId();
        symInfo->valSymMap.insert(std::make_pair(svfVal, id));
        DBOUT(DMemModel,
              outs() << "create a new value sym " << id << "\n");
        ///  handle global constant expression here
        if (const GlobalVariable* globalVar = SVFUtil::dyn_cast<GlobalVariable>(val))
            handleGlobalCE(globalVar);
    }

    if (isConstantObjSym(val))
        collectObj(val);
}

/*!
 * Get memory object sym, if not available create a new one
 */
void SymbolTableBuilder::collectObj(const Value *val)
{
    val = LLVMUtil::getGlobalRep(val);
    SymbolTableInfo::ValueToIDMapTy::iterator iter = symInfo->objSymMap.find(LLVMModuleSet::getLLVMModuleSet()->getSVFValue(val));
    if (iter == symInfo->objSymMap.end())
    {
        SVFValue* svfVal = LLVMModuleSet::getLLVMModuleSet()->getSVFValue(val);
        // if the object pointed by the pointer is a constant data (e.g., i32 0) or a global constant object (e.g. string)
        // then we treat them as one ConstantObj
        if((isConstantObjSym(val) && !symInfo->getModelConstants()))
        {
            symInfo->objSymMap.insert(std::make_pair(svfVal, symInfo->constantSymID()));
        }
        // otherwise, we will create an object for each abstract memory location
        else
        {
            // create obj sym and sym type
            SymID id = NodeIDAllocator::get()->allocateObjectId();
            symInfo->objSymMap.insert(std::make_pair(svfVal, id));
            DBOUT(DMemModel,
                  outs() << "create a new obj sym " << id << "\n");

            // create a memory object
            MemObj* mem = new MemObj(id, createObjTypeInfo(val), LLVMModuleSet::getLLVMModuleSet()->getSVFValue(val));
            assert(symInfo->objMap.find(id) == symInfo->objMap.end());
            symInfo->objMap[id] = mem;
        }
    }
}

/*!
 * Create unique return sym, if not available create a new one
 */
void SymbolTableBuilder::collectRet(const Function *val)
{
    const SVFFunction* svffun = LLVMModuleSet::getLLVMModuleSet()->getSVFFunction(val);
    SymbolTableInfo::FunToIDMapTy::iterator iter = symInfo->returnSymMap.find(svffun);
    if (iter == symInfo->returnSymMap.end())
    {
        SymID id = NodeIDAllocator::get()->allocateValueId();
        symInfo->returnSymMap.insert(std::make_pair(svffun, id));
        DBOUT(DMemModel,
              outs() << "create a return sym " << id << "\n");
    }
}

/*!
 * Create vararg sym, if not available create a new one
 */
void SymbolTableBuilder::collectVararg(const Function *val)
{
    const SVFFunction* svffun = LLVMModuleSet::getLLVMModuleSet()->getSVFFunction(val);
    SymbolTableInfo::FunToIDMapTy::iterator iter = symInfo->varargSymMap.find(svffun);
    if (iter == symInfo->varargSymMap.end())
    {
        SymID id = NodeIDAllocator::get()->allocateValueId();
        symInfo->varargSymMap.insert(std::make_pair(svffun, id));
        DBOUT(DMemModel,
              outs() << "create a vararg sym " << id << "\n");
    }
}


/*!
 * Handle constant expression
 */
void SymbolTableBuilder::handleCE(const Value *val)
{
    if (const Constant* ref = SVFUtil::dyn_cast<Constant>(val))
    {
        if (const ConstantExpr* ce = isGepConstantExpr(ref))
        {
            DBOUT(DMemModelCE,
                  outs() << "handle constant expression " << SVFUtil::value2String(LLVMModuleSet::getLLVMModuleSet()->getSVFValue(ref)) << "\n");
            collectVal(ce);
            collectVal(ce->getOperand(0));
            // handle the recursive constant express case
            // like (gep (bitcast (gep X 1)) 1); the inner gep is ce->getOperand(0)
            handleCE(ce->getOperand(0));
        }
        else if (const ConstantExpr* ce = isCastConstantExpr(ref))
        {
            DBOUT(DMemModelCE,
                  outs() << "handle constant expression " << SVFUtil::value2String(LLVMModuleSet::getLLVMModuleSet()->getSVFValue(ref)) << "\n");
            collectVal(ce);
            collectVal(ce->getOperand(0));
            // handle the recursive constant express case
            // like (gep (bitcast (gep X 1)) 1); the inner gep is ce->getOperand(0)
            handleCE(ce->getOperand(0));
        }
        else if (const ConstantExpr* ce = isSelectConstantExpr(ref))
        {
            DBOUT(DMemModelCE,
                  outs() << "handle constant expression " << SVFUtil::value2String(LLVMModuleSet::getLLVMModuleSet()->getSVFValue(ref)) << "\n");
            collectVal(ce);
            collectVal(ce->getOperand(0));
            collectVal(ce->getOperand(1));
            collectVal(ce->getOperand(2));
            // handle the recursive constant express case
            // like (gep (bitcast (gep X 1)) 1); the inner gep is ce->getOperand(0)
            handleCE(ce->getOperand(0));
            handleCE(ce->getOperand(1));
            handleCE(ce->getOperand(2));
        }
        // if we meet a int2ptr, then it points-to black hole
        else if (const ConstantExpr *int2Ptrce = isInt2PtrConstantExpr(ref))
        {
            collectVal(int2Ptrce);
        }
        else if (const ConstantExpr *ptr2Intce = isPtr2IntConstantExpr(ref))
        {
            collectVal(ptr2Intce);
            const Constant *opnd = ptr2Intce->getOperand(0);
            handleCE(opnd);
        }
        else if (isTruncConstantExpr(ref) || isCmpConstantExpr(ref))
        {
            collectVal(ref);
        }
        else if (isBinaryConstantExpr(ref))
        {
            collectVal(ref);
        }
        else if (isUnaryConstantExpr(ref))
        {
            // we don't handle unary constant expression like fneg(x) now
            collectVal(ref);
        }
        else if (SVFUtil::isa<ConstantAggregate>(ref))
        {
            // we don't handle constant agrgregate like constant vectors
            collectVal(ref);
        }
        else
        {
            if (SVFUtil::isa<ConstantExpr>(val))
                assert(false && "we don't handle all other constant expression for now!");
            collectVal(ref);
        }
    }
}

/*!
 * Handle global constant expression
 */
void SymbolTableBuilder::handleGlobalCE(const GlobalVariable *G)
{
    assert(G);

    //The type this global points to
    const Type *T = G->getValueType();
    bool is_array = 0;
    //An array is considered a single variable of its type.
    while (const ArrayType *AT = SVFUtil::dyn_cast<ArrayType>(T))
    {
        T = AT->getElementType();
        is_array = 1;
    }

    if (SVFUtil::isa<StructType>(T))
    {
        //A struct may be used in constant GEP expr.
        for (Value::const_user_iterator it = G->user_begin(), ie = G->user_end();
                it != ie; ++it)
        {
            handleCE(*it);
        }
    }
    else
    {
        if (is_array)
        {
            for (Value::const_user_iterator it = G->user_begin(), ie =
                        G->user_end(); it != ie; ++it)
            {
                handleCE(*it);
            }
        }
    }

    if (G->hasInitializer())
    {
        handleGlobalInitializerCE(G->getInitializer());
    }
}

/*!
 * Handle global variable initialization
 */
void SymbolTableBuilder::handleGlobalInitializerCE(const Constant *C)
{

    if (C->getType()->isSingleValueType())
    {
        if (const ConstantExpr *E = SVFUtil::dyn_cast<ConstantExpr>(C))
        {
            handleCE(E);
        }
        else
        {
            collectVal(C);
        }
    }
    else if (SVFUtil::isa<ConstantArray>(C))
    {
        for (u32_t i = 0, e = C->getNumOperands(); i != e; i++)
        {
            handleGlobalInitializerCE(SVFUtil::cast<Constant>(C->getOperand(i)));
        }
    }
    else if (SVFUtil::isa<ConstantStruct>(C))
    {
        for (u32_t i = 0, e = C->getNumOperands(); i != e; i++)
        {
            handleGlobalInitializerCE(SVFUtil::cast<Constant>(C->getOperand(i)));
        }
    }
    else if(const ConstantData* data = SVFUtil::dyn_cast<ConstantData>(C))
    {
        if(Options::ModelConsts)
        {
            if(const ConstantDataSequential* seq = SVFUtil::dyn_cast<ConstantDataSequential>(data))
            {
                for(u32_t i = 0; i < seq->getNumElements(); i++)
                {
                    const Constant* ct = seq->getElementAsConstant(i);
                    handleGlobalInitializerCE(ct);
                }
            }
            else
            {
                assert((SVFUtil::isa<ConstantAggregateZero>(data) || SVFUtil::isa<UndefValue>(data)) && "Single value type data should have been handled!");
            }
        }
    }
    else
    {
        //TODO:assert(SVFUtil::isa<ConstantVector>(C),"what else do we have");
    }
}

/*
 * Initial the memory object here
 */
ObjTypeInfo* SymbolTableBuilder::createObjTypeInfo(const Value *val)
{
    const PointerType *refTy = nullptr;

    const Instruction* I = SVFUtil::dyn_cast<Instruction>(val);

    // We consider two types of objects:
    // (1) A heap/static object from a callsite
    if (I && isNonInstricCallSite(LLVMModuleSet::getLLVMModuleSet()->getSVFInstruction(I)))
    {
        const SVFInstruction* svfInst = LLVMModuleSet::getLLVMModuleSet()->getSVFInstruction(I);
        refTy = getRefTypeOfHeapAllocOrStatic(svfInst);
    }
    // (2) Other objects (e.g., alloca, global, etc.)
    else
        refTy = SVFUtil::dyn_cast<PointerType>(val->getType());

    if (refTy)
    {
        Type *objTy = getPtrElementType(refTy);
        ObjTypeInfo* typeInfo = new ObjTypeInfo(objTy, Options::MaxFieldLimit);
        initTypeInfo(typeInfo,val);
        return typeInfo;
    }
    else
    {
        writeWrnMsg("try to create an object with a non-pointer type.");
        writeWrnMsg(val->getName().str());
        writeWrnMsg("(" + getSourceLoc(LLVMModuleSet::getLLVMModuleSet()->getSVFValue(val)) + ")");
        if(isConstantObjSym(val))
        {
            ObjTypeInfo* typeInfo = new ObjTypeInfo(val->getType(), 0);
            initTypeInfo(typeInfo,val);
            return typeInfo;
        }
        else
        {
            assert(false && "Memory object must be either (1) held by a pointer-typed ref value or (2) a constant value (e.g., 10).");
            abort();
        }
    }
}

/*!
 * Analyse types of all flattened fields of this object
 */
void SymbolTableBuilder::analyzeObjType(ObjTypeInfo* typeinfo, const Value* val)
{

    const PointerType * refty = SVFUtil::dyn_cast<PointerType>(val->getType());
    assert(refty && "this value should be a pointer type!");
    Type* elemTy = getPtrElementType(refty);
    bool isPtrObj = false;
    // Find the inter nested array element
    while (const ArrayType *AT= SVFUtil::dyn_cast<ArrayType>(elemTy))
    {
        elemTy = AT->getElementType();
        if(elemTy->isPointerTy())
            isPtrObj = true;
        if(SVFUtil::isa<GlobalVariable>(val) && SVFUtil::cast<GlobalVariable>(val)->hasInitializer()
                && SVFUtil::isa<ConstantArray>(SVFUtil::cast<GlobalVariable>(val)->getInitializer()))
            typeinfo->setFlag(ObjTypeInfo::CONST_ARRAY_OBJ);
        else
            typeinfo->setFlag(ObjTypeInfo::VAR_ARRAY_OBJ);
    }
    if (const StructType *ST= SVFUtil::dyn_cast<StructType>(elemTy))
    {
        const std::vector<const Type*>& flattenFields = getStructInfo(ST)->getFlattenFieldTypes();
        for(std::vector<const Type*>::const_iterator it = flattenFields.begin(), eit = flattenFields.end();
                it!=eit; ++it)
        {
            if((*it)->isPointerTy())
                isPtrObj = true;
        }
        if(SVFUtil::isa<GlobalVariable>(val) && SVFUtil::cast<GlobalVariable>(val)->hasInitializer()
                && SVFUtil::isa<ConstantStruct>(SVFUtil::cast<GlobalVariable>(val)->getInitializer()))
            typeinfo->setFlag(ObjTypeInfo::CONST_STRUCT_OBJ);
        else
            typeinfo->setFlag(ObjTypeInfo::VAR_STRUCT_OBJ);
    }
    else if (elemTy->isPointerTy())
    {
        isPtrObj = true;
    }

    if(isPtrObj)
        typeinfo->setFlag(ObjTypeInfo::HASPTR_OBJ);
}

/*!
 * Analyse types of heap and static objects
 */
void SymbolTableBuilder::analyzeHeapObjType(ObjTypeInfo* typeinfo, const Value* val)
{
    if(const Value* castUse = getUniqueUseViaCastInst(val))
    {
        typeinfo->setFlag(ObjTypeInfo::HEAP_OBJ);
        typeinfo->resetTypeForHeapStaticObj(castUse->getType());
        analyzeObjType(typeinfo,castUse);
    }
    else
    {
        typeinfo->setFlag(ObjTypeInfo::HEAP_OBJ);
        typeinfo->setFlag(ObjTypeInfo::HASPTR_OBJ);
    }
}

/*!
 * Analyse types of heap and static objects
 */
void SymbolTableBuilder::analyzeStaticObjType(ObjTypeInfo* typeinfo, const Value* val)
{
    if(const Value* castUse = getUniqueUseViaCastInst(val))
    {
        typeinfo->setFlag(ObjTypeInfo::STATIC_OBJ);
        typeinfo->resetTypeForHeapStaticObj(castUse->getType());
        analyzeObjType(typeinfo,castUse);
    }
    else
    {
        typeinfo->setFlag(ObjTypeInfo::HEAP_OBJ);
        typeinfo->setFlag(ObjTypeInfo::HASPTR_OBJ);
    }
}

/*!
 * Initialize the type info of an object
 */
void SymbolTableBuilder::initTypeInfo(ObjTypeInfo* typeinfo, const Value* val)
{

    u32_t objSize = 1;
    // Global variable
    if (SVFUtil::isa<Function>(val))
    {
        typeinfo->setFlag(ObjTypeInfo::FUNCTION_OBJ);
        analyzeObjType(typeinfo,val);
        objSize = getObjSize(typeinfo->getType());
    }
    else if(const AllocaInst* allocaInst = SVFUtil::dyn_cast<AllocaInst>(val))
    {
        typeinfo->setFlag(ObjTypeInfo::STACK_OBJ);
        analyzeObjType(typeinfo,val);
        /// This is for `alloca <ty> <NumElements>`. For example, `alloca i64 3` allocates 3 i64 on the stack (objSize=3)
        /// In most cases, `NumElements` is not specified in the instruction, which means there is only one element (objSize=1).
        if(const ConstantInt* sz = SVFUtil::dyn_cast<ConstantInt>(allocaInst->getArraySize()))
            objSize = sz->getZExtValue() * getObjSize(typeinfo->getType());
        else
            objSize = getObjSize(typeinfo->getType());
    }
    else if(SVFUtil::isa<GlobalVariable>(val))
    {
        typeinfo->setFlag(ObjTypeInfo::GLOBVAR_OBJ);
        if(isConstantObjSym(val))
            typeinfo->setFlag(ObjTypeInfo::CONST_GLOBAL_OBJ);
        analyzeObjType(typeinfo,val);
        objSize = getObjSize(typeinfo->getType());
    }
    else if (SVFUtil::isa<Instruction>(val) && isHeapAllocExtCall(LLVMModuleSet::getLLVMModuleSet()->getSVFInstruction(SVFUtil::cast<Instruction>(val))))
    {
        analyzeHeapObjType(typeinfo,val);
        // Heap object, label its field as infinite here
        objSize = typeinfo->getMaxFieldOffsetLimit();
    }
    else if (SVFUtil::isa<Instruction>(val) && isStaticExtCall(LLVMModuleSet::getLLVMModuleSet()->getSVFInstruction(SVFUtil::cast<Instruction>(val))))
    {
        analyzeStaticObjType(typeinfo,val);
        // static object allocated before main, label its field as infinite here
        objSize = typeinfo->getMaxFieldOffsetLimit();
    }
    else if(ArgInProgEntryFunction(val))
    {
        analyzeStaticObjType(typeinfo,val);
        // user input data, label its field as infinite here
        objSize = typeinfo->getMaxFieldOffsetLimit();
    }
    else if(LLVMUtil::isConstDataOrAggData(val))
    {
        typeinfo->setFlag(ObjTypeInfo::CONST_DATA);
        objSize = getNumOfFlattenElements(val->getType());
    }
    else
    {
        assert("what other object do we have??");
        abort();
    }

    // Reset maxOffsetLimit if it is over the total fieldNum of this object
    if(typeinfo->getMaxFieldOffsetLimit() > objSize)
        typeinfo->setNumOfElements(objSize);
}

/*!
 * Return size of this Object
 */
u32_t SymbolTableBuilder::getObjSize(const Type* ety)
{
    assert(ety && "type is null?");
    u32_t numOfFields = 1;
    if (SVFUtil::isa<StructType>(ety) || SVFUtil::isa<ArrayType>(ety))
    {
        numOfFields = getNumOfFlattenElements(ety);
    }
    return numOfFields;
}

/*!
 * Check whether this value points-to a constant object
 */
bool SymbolTableBuilder::isConstantObjSym(const Value* val)
{
    if (const GlobalVariable* v = SVFUtil::dyn_cast<GlobalVariable>(val))
    {
        if (cppUtil::isValVtbl(v))
            return false;
        else if (!v->hasInitializer())
        {
            if(v->isExternalLinkage(v->getLinkage()))
                return false;
            else
                return true;
        }
        else
        {
            StInfo *stInfo = getStructInfo(v->getInitializer()->getType());
            const std::vector<const Type*> &fields = stInfo->getFlattenFieldTypes();
            for (std::vector<const Type*>::const_iterator it = fields.begin(), eit = fields.end(); it != eit; ++it)
            {
                const Type *elemTy = *it;
                assert(!SVFUtil::isa<FunctionType>(elemTy) && "Initializer of a global is a function?");
                if (SVFUtil::isa<PointerType>(elemTy))
                    return false;
            }

            return v->isConstant();
        }
    }
    return LLVMUtil::isConstDataOrAggData(val);
}

/// Number of flattenned elements of an array or struct
u32_t SymbolTableBuilder::getNumOfFlattenElements(const Type *T)
{
    if(Options::ModelArrays)
        return getStructInfo(T)->getNumOfFlattenElements();
    else
        return getStructInfo(T)->getNumOfFlattenFields();
}


StInfo* SymbolTableBuilder::getStructInfo(const Type *T)
{
    assert(T);
    if (symInfo->hasTypeInfo(T))
        return symInfo->getTypeInfo(T);
    else
    {
        collectTypeInfo(T);
        return symInfo->getTypeInfo(T);
    }
}

/*!
 * Collect a LLVM type info
 */
void SymbolTableBuilder::collectTypeInfo(const Type* ty)
{
    assert(!symInfo->hasTypeInfo(ty) && "this type has been collected before");

    if (const ArrayType* aty = SVFUtil::dyn_cast<ArrayType>(ty))
        collectArrayInfo(aty);
    else if (const StructType* sty = SVFUtil::dyn_cast<StructType>(ty))
        collectStructInfo(sty);
    else
        collectSimpleTypeInfo(ty);
}


/*!
 * Fill in StInfo for an array type.
 */
void SymbolTableBuilder::collectArrayInfo(const ArrayType* ty)
{
    u64_t totalElemNum = ty->getNumElements();
    const Type* elemTy = ty->getElementType();
    while (const ArrayType* aty = SVFUtil::dyn_cast<ArrayType>(elemTy))
    {
        totalElemNum *= aty->getNumElements();
        elemTy = aty->getElementType();
    }

    StInfo* stinfo = new StInfo(totalElemNum);
    symInfo->addTypeInfo(ty, stinfo);

    /// array without any element (this is not true in C/C++ arrays) we assume there is an empty dummy element
    if(totalElemNum==0)
    {
        stinfo->addFldWithType(0, elemTy, 0);
        stinfo->setNumOfFieldsAndElems(1, 1);
        stinfo->getFlattenFieldTypes().push_back(elemTy);
        stinfo->getFlattenElementTypes().push_back(elemTy);
        return;
    }

    /// Array's flatten field infor is the same as its element's
    /// flatten infor.
    StInfo* elemStInfo = getStructInfo(elemTy);
    u32_t nfE = elemStInfo->getNumOfFlattenFields();
    for (u32_t j = 0; j < nfE; j++)
    {
        const Type* fieldTy = elemStInfo->getFlattenFieldTypes()[j];
        stinfo->getFlattenFieldTypes().push_back(fieldTy);
    }

    /// Flatten arrays, map each array element index `i` to flattened index `(i * nfE * totalElemNum)/outArrayElemNum`
    /// nfE>1 if the array element is a struct with more than one field.
    u32_t outArrayElemNum = ty->getNumElements();
    for(u32_t i = 0; i < outArrayElemNum; i++)
        stinfo->addFldWithType(0, elemTy, (i * nfE * totalElemNum)/outArrayElemNum);

    for(u32_t i = 0; i < totalElemNum; i++)
    {
        for(u32_t j = 0; j < nfE; j++)
        {
            stinfo->getFlattenElementTypes().push_back(elemStInfo->getFlattenFieldTypes()[j]);
        }
    }

    assert(stinfo->getFlattenElementTypes().size() == nfE * totalElemNum && "typeForArray size incorrect!!!");
    stinfo->setNumOfFieldsAndElems(nfE, nfE * totalElemNum);
}


/*!
 * Fill in struct_info for T.
 * Given a Struct type, we recursively extend and record its fields and types.
 */
void SymbolTableBuilder::collectStructInfo(const StructType *sty)
{
    /// The struct info should not be processed before
    StInfo* stinfo = new StInfo(1);
    symInfo->addTypeInfo(sty, stinfo);

    // Number of fields after flattening the struct
    u32_t nf = 0;
    // The offset when considering array stride info
    u32_t strideOffset = 0;
    for (StructType::element_iterator it = sty->element_begin(), ie =
                sty->element_end(); it != ie; ++it)
    {
        const Type *et = *it;
        /// offset with int_32 (s32_t) is large enough and will not cause overflow
        stinfo->addFldWithType(nf, et, strideOffset);

        if (SVFUtil::isa<StructType>(et) || SVFUtil::isa<ArrayType>(et))
        {
            StInfo * subStinfo = getStructInfo(et);
            u32_t nfE = subStinfo->getNumOfFlattenFields();
            //Copy ST's info, whose element 0 is the size of ST itself.
            for (u32_t j = 0; j < nfE; j++)
            {
                const Type* elemTy = subStinfo->getFlattenFieldTypes()[j];
                stinfo->getFlattenFieldTypes().push_back(elemTy);
            }
            nf += nfE;
            strideOffset += nfE * subStinfo->getStride();
            for(u32_t tpi = 0; tpi < subStinfo->getStride(); tpi++)
            {
                for(u32_t tpj = 0; tpj < nfE; tpj++)
                {
                    stinfo->getFlattenElementTypes().push_back(subStinfo->getFlattenFieldTypes()[tpj]);
                }
            }
        }
        else     //simple type
        {
            nf += 1;
            strideOffset += 1;
            stinfo->getFlattenFieldTypes().push_back(et);
            stinfo->getFlattenElementTypes().push_back(et);
        }
    }

    assert(stinfo->getFlattenElementTypes().size() == strideOffset && "typeForStruct size incorrect!");
    stinfo->setNumOfFieldsAndElems(nf,strideOffset);

    //Record the size of the complete struct and update max_struct.
    if (nf > symInfo->maxStSize)
    {
        symInfo->maxStruct = sty;
        symInfo->maxStSize = nf;
    }
}


/*!
 * Collect simple type (non-aggregate) info
 */
void SymbolTableBuilder::collectSimpleTypeInfo(const Type* ty)
{
    StInfo* stinfo = new StInfo(1);
    symInfo->addTypeInfo(ty, stinfo);

    /// Only one field
    stinfo->addFldWithType(0, ty, 0);

    stinfo->getFlattenFieldTypes().push_back(ty);
    stinfo->getFlattenElementTypes().push_back(ty);
    stinfo->setNumOfFieldsAndElems(1,1);
}