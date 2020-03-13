//===- ThreadCallGraph.cpp -- Call graph considering thread fork/join---------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2017>  <Yulei Sui>
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
 * ThreadCallGraph.cpp
 *
 *  Created on: Jul 12, 2014
 *      Author: Yulei Sui, Peng Di, Ding Ye
 */

#include "Util/SVFModule.h"
#include "Util/ThreadCallGraph.h"

using namespace SVFUtil;

/*!
 * Constructor
 */
ThreadCallGraph::ThreadCallGraph(SVFModule svfModule) :
    PTACallGraph(svfModule, ThdCallGraph), tdAPI(ThreadAPI::getThreadAPI()) {
    DBOUT(DGENERAL, outs() << SVFUtil::pasMsg("Building ThreadCallGraph\n"));
    this->build(svfModule);
}

/*!
 * Start building Thread Call Graph
 */
void ThreadCallGraph::build(SVFModule svfModule) {
    // create thread fork edges and record fork sites
    for (SVFModule::const_iterator fi = svfModule.begin(), efi = svfModule.end(); fi != efi; ++fi) {
        const Function *fun = *fi;
        for (const_inst_iterator II = inst_begin(*fun), E = inst_end(*fun); II != E; ++II) {
            const Instruction *inst = &*II;
            if (tdAPI->isTDFork(inst)) {
            	CallSite cs = getLLVMCallSite(inst);
                addForksite(cs);
                const Function* forkee = SVFUtil::dyn_cast<Function>(tdAPI->getForkedFun(inst));
                if (forkee) {
                    addDirectForkEdge(cs);
                }
                // indirect call to the start routine function
                else {
                    addThreadForkEdgeSetMap(cs,NULL);
                }
            }
            else if (tdAPI->isHareParFor(inst)) {
            	CallSite cs = getLLVMCallSite(inst);
                addParForSite(cs);
                const Function* taskFunc = SVFUtil::dyn_cast<Function>(tdAPI->getTaskFuncAtHareParForSite(inst));
                if (taskFunc) {
                    addDirectParForEdge(cs);
                }
                // indirect call to the start routine function
                else {
                    addHareParForEdgeSetMap(cs,NULL);
                }
            }
        }
    }
    // record join sites
    for (SVFModule::const_iterator fi = svfModule.begin(), efi = svfModule.end(); fi != efi; ++fi) {
        const Function *fun = *fi;
        for (const_inst_iterator II = inst_begin(*fun), E = inst_end(*fun); II != E; ++II) {
            const Instruction *inst = &*II;
            if (tdAPI->isTDJoin(inst)) {
            	CallSite cs = getLLVMCallSite(inst);
                addJoinsite(cs);
            }
        }
    }
}

/*
 * Update call graph using pointer analysis results
 * (1) resolve function pointers for non-fork calls
 * (2) resolve function pointers for fork sites
 * (3) resolve function pointers for parallel_for sites
 */
void ThreadCallGraph::updateCallGraph(PointerAnalysis* pta) {

    PointerAnalysis::CallEdgeMap::const_iterator iter = pta->getIndCallMap().begin();
    PointerAnalysis::CallEdgeMap::const_iterator eiter = pta->getIndCallMap().end();
    for (; iter != eiter; iter++) {
        CallSite cs = iter->first;
        const Instruction *callInst = cs.getInstruction();
        const PTACallGraph::FunctionSet &functions = iter->second;
        for (PTACallGraph::FunctionSet::const_iterator func_iter =
                    functions.begin(); func_iter != functions.end(); func_iter++) {
            const Function *callee = *func_iter;
            this->addIndirectCallGraphEdge(cs, callee);
        }
    }

    // Fork sites
    for (CallSiteSet::iterator it = forksitesBegin(), eit = forksitesEnd(); it != eit; ++it) {
        const Value* forkedval = tdAPI->getForkedFun(*it);
        if(SVFUtil::dyn_cast<Function>(forkedval)==NULL) {
            PAG* pag = pta->getPAG();
            const PointsTo& targets = pta->getPts(pag->getValueNode(forkedval));
            for (PointsTo::iterator ii = targets.begin(), ie = targets.end(); ii != ie; ii++) {
                if(ObjPN* objPN = SVFUtil::dyn_cast<ObjPN>(pag->getPAGNode(*ii))) {
                    const MemObj* obj = pag->getObject(objPN);
                    if(obj->isFunction()) {
                        const Function* callee = SVFUtil::cast<Function>(obj->getRefVal());
                        this->addIndirectForkEdge(*it, callee);
                    }
                }
            }
        }
    }

    // parallel_for sites
    for (CallSiteSet::iterator it = parForSitesBegin(), eit = parForSitesEnd(); it != eit; ++it) {
        const Value* forkedval = tdAPI->getTaskFuncAtHareParForSite(*it);
        if(SVFUtil::dyn_cast<Function>(forkedval)==NULL) {
            PAG* pag = pta->getPAG();
            const PointsTo& targets = pta->getPts(pag->getValueNode(forkedval));
            for (PointsTo::iterator ii = targets.begin(), ie = targets.end(); ii != ie; ii++) {
                if(ObjPN* objPN = SVFUtil::dyn_cast<ObjPN>(pag->getPAGNode(*ii))) {
                    const MemObj* obj = pag->getObject(objPN);
                    if(obj->isFunction()) {
                        const Function* callee = SVFUtil::cast<Function>(obj->getRefVal());
                        this->addIndirectForkEdge(*it, callee);
                    }
                }
            }
        }
    }
}


/*!
 * Update join edge using pointer analysis results
 */
void ThreadCallGraph::updateJoinEdge(PointerAnalysis* pta) {

    for (CallSiteSet::iterator it = joinsitesBegin(), eit = joinsitesEnd(); it != eit; ++it) {
        const Value* jointhread = tdAPI->getJoinedThread(*it);
        // find its corresponding fork sites first
        CallSiteSet forkset;
        for (CallSiteSet::iterator it = forksitesBegin(), eit = forksitesEnd(); it != eit; ++it) {
            const Value* forkthread = tdAPI->getForkedThread(*it);
            if (pta->alias(jointhread, forkthread)) {
                forkset.insert(*it);
            }
        }
        assert(!forkset.empty() && "Can't find a forksite for this join!!");
        addDirectJoinEdge(*it,forkset);
    }
}

/*!
 * Add direct fork edges
 */
void ThreadCallGraph::addDirectForkEdge(CallSite cs) {
	const Instruction* call = cs.getInstruction();

    PTACallGraphNode* caller = getCallGraphNode(call->getParent()->getParent());
    const Function* forkee = SVFUtil::dyn_cast<Function>(tdAPI->getForkedFun(call));
    assert(forkee && "callee does not exist");
    PTACallGraphNode* callee = getCallGraphNode(forkee);
    callee = getCallGraphNode(getDefFunForMultipleModule(forkee));
    CallSiteID csId = addCallSite(cs, callee->getFunction());

    if (!hasGraphEdge(caller, callee, PTACallGraphEdge::TDForkEdge, csId)) {
        assert(call->getParent()->getParent() == caller->getFunction() && "callee instruction not inside caller??");

        ThreadForkEdge* edge = new ThreadForkEdge(caller, callee, csId);
        edge->addDirectCallSite(cs);

        addEdge(edge);
        addThreadForkEdgeSetMap(cs, edge);
    }
}

/*!
 * Add indirect fork edge to update call graph
 */
void ThreadCallGraph::addIndirectForkEdge(CallSite cs, const Function* calleefun) {
	const Instruction* call = cs.getInstruction();
    PTACallGraphNode* caller = getCallGraphNode(call->getParent()->getParent());
    PTACallGraphNode* callee = getCallGraphNode(calleefun);

    CallSiteID csId = addCallSite(cs, callee->getFunction());

    if (!hasGraphEdge(caller, callee, PTACallGraphEdge::TDForkEdge, csId)) {
        assert(call->getParent()->getParent() == caller->getFunction() && "callee instruction not inside caller??");

        ThreadForkEdge* edge = new ThreadForkEdge(caller, callee, csId);
        edge->addInDirectCallSite(cs);

        addEdge(edge);
        addThreadForkEdgeSetMap(cs, edge);
    }
}

/*!
 * Add direct fork edges
 * As join edge is a special return which is back to join site(s) rather than its fork site
 * A ThreadJoinEdge is created from the functions where join sites reside in to the start routine function
 * But we don't invoke addEdge() method to add the edge to src and dst, otherwise it makes a scc cycle
 */
void ThreadCallGraph::addDirectJoinEdge(CallSite cs,const CallSiteSet& forkset) {

	const Instruction* call = cs.getInstruction();

    PTACallGraphNode* joinFunNode = getCallGraphNode(call->getParent()->getParent());

    for (CallSiteSet::const_iterator it = forkset.begin(), eit = forkset.end(); it != eit; ++it) {

        CallSite forksite = *it;
        const Function* threadRoutineFun = SVFUtil::dyn_cast<Function>(tdAPI->getForkedFun(forksite));
        assert(threadRoutineFun && "thread routine function does not exist");
        PTACallGraphNode* threadRoutineFunNode = getCallGraphNode(threadRoutineFun);
        CallSite cs = SVFUtil::getLLVMCallSite(call);
        CallSiteID csId = addCallSite(cs, threadRoutineFun);

        if (!hasThreadJoinEdge(cs,joinFunNode,threadRoutineFunNode, csId)) {
            assert(call->getParent()->getParent() == joinFunNode->getFunction() && "callee instruction not inside caller??");
            ThreadJoinEdge* edge = new ThreadJoinEdge(joinFunNode,threadRoutineFunNode,csId);
            edge->addDirectCallSite(cs);

            addThreadJoinEdgeSetMap(cs, edge);
        }
    }
}

/*!
 * Add a direct ParFor edges
 */
void ThreadCallGraph::addDirectParForEdge(CallSite cs) {

	const Instruction* call = cs.getInstruction();

    PTACallGraphNode* caller = getCallGraphNode(call->getParent()->getParent());
    const Function* taskFunc = SVFUtil::dyn_cast<Function>(tdAPI->getTaskFuncAtHareParForSite(call));
    assert(taskFunc && "callee does not exist");
    PTACallGraphNode* callee = getCallGraphNode(taskFunc);

    CallSiteID csId = addCallSite(cs, callee->getFunction());

    if (!hasGraphEdge(caller, callee, PTACallGraphEdge::TDForkEdge, csId)) {
        assert(call->getParent()->getParent() == caller->getFunction() && "callee instruction not inside caller??");

        HareParForEdge* edge = new HareParForEdge(caller, callee, csId);
        edge->addDirectCallSite(cs);

        addEdge(edge);
        addHareParForEdgeSetMap(cs, edge);
    }
}

/*!
 * Add an indirect ParFor edge to update call graph
 */
void ThreadCallGraph::addIndirectParForEdge(CallSite cs, const Function* calleefun) {
	const Instruction* call = cs.getInstruction();

    PTACallGraphNode* caller = getCallGraphNode(call->getParent()->getParent());
    PTACallGraphNode* callee = getCallGraphNode(calleefun);

    CallSiteID csId = addCallSite(cs, callee->getFunction());

    if (!hasGraphEdge(caller, callee, PTACallGraphEdge::HareParForEdge,csId)) {
        assert(call->getParent()->getParent() == caller->getFunction() && "callee instruction not inside caller??");

        HareParForEdge* edge = new HareParForEdge(caller, callee, csId);
        edge->addInDirectCallSite(cs);

        addEdge(edge);
        addHareParForEdgeSetMap(cs, edge);
    }
}
