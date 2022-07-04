//===----- CFLAlias.cpp -- CFL Alias Analysis Client--------------//
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
 * CFLAlias.cpp
 *
 *  Created on: June 27 , 2022
 *      Author: Pei Xu
 */

#include "CFL/CFLAlias.h"
#include "Util/SVFBasicTypes.h"

using namespace SVF;
using namespace cppUtil;
using namespace SVFUtil;

/*!
 * On the fly call graph construction
 * callsites is candidate indirect callsites need to be analyzed based on points-to results
 * newEdges is the new indirect call edges discovered
 */
void CFLAlias::onTheFlyCallGraphSolve(const CallSiteToFunPtrMap& callsites, CallEdgeMap& newEdges)
{
    for(CallSiteToFunPtrMap::const_iterator iter = callsites.begin(), eiter = callsites.end(); iter!=eiter; ++iter)
    {
        const CallICFGNode* cs = iter->first;

        if (isVirtualCallSite(SVFUtil::getLLVMCallSite(cs->getCallSite())))
        {
            const Value *vtbl = getVCallVtblPtr(SVFUtil::getLLVMCallSite(cs->getCallSite()));
            assert(pag->hasValueNode(vtbl));
            NodeID vtblId = pag->getValueNode(vtbl);
            resolveCPPIndCalls(cs, getPts(vtblId), newEdges);
        }
        else
            resolveIndCalls(iter->first,getPts(iter->second),newEdges);
    }
}

/*!
 * Connect formal and actual parameters for indirect callsites
 */

void CFLAlias::connectCaller2CalleeParams(CallSite cs, const SVFFunction* F)
{
    assert(F);

    DBOUT(DAndersen, outs() << "connect parameters from indirect callsite " << SVFUtil::value2String(cs.getInstruction()) << " to callee " << *F << "\n");

    CallICFGNode* callBlockNode = svfir->getICFG()->getCallICFGNode(cs.getInstruction());
    RetICFGNode* retBlockNode = svfir->getICFG()->getRetICFGNode(cs.getInstruction());

    // if(SVFUtil::isHeapAllocExtFunViaRet(F) && svfir->callsiteHasRet(retBlockNode))
    // {
    //     heapAllocatorViaIndCall(cs,cpySrcNodes);
    // }

    if (svfir->funHasRet(F) && svfir->callsiteHasRet(retBlockNode))
    {
        const PAGNode* cs_return = svfir->getCallSiteRet(retBlockNode);
        const PAGNode* fun_return = svfir->getFunRet(F);
        if (cs_return->isPointer() && fun_return->isPointer())
        {
            NodeID dstrec = cs_return->getId();
            NodeID srcret = fun_return->getId();
            addCopyEdge(srcret, dstrec);
        }
        else
        {
            DBOUT(DAndersen, outs() << "not a pointer ignored\n");
        }
    }

    if (svfir->hasCallSiteArgsMap(callBlockNode) && svfir->hasFunArgsList(F))
    {

        // connect actual and formal param
        const SVFIR::SVFVarList& csArgList = svfir->getCallSiteArgsList(callBlockNode);
        const SVFIR::SVFVarList& funArgList = svfir->getFunArgsList(F);
        //Go through the fixed parameters.
        DBOUT(DPAGBuild, outs() << "      args:");
        SVFIR::SVFVarList::const_iterator funArgIt = funArgList.begin(), funArgEit = funArgList.end();
        SVFIR::SVFVarList::const_iterator csArgIt  = csArgList.begin(), csArgEit = csArgList.end();
        for (; funArgIt != funArgEit; ++csArgIt, ++funArgIt)
        {
            //Some programs (e.g. Linux kernel) leave unneeded parameters empty.
            if (csArgIt  == csArgEit)
            {
                DBOUT(DAndersen, outs() << " !! not enough args\n");
                break;
            }
            const PAGNode *cs_arg = *csArgIt ;
            const PAGNode *fun_arg = *funArgIt;

            if (cs_arg->isPointer() && fun_arg->isPointer())
            {
                DBOUT(DAndersen, outs() << "process actual parm  " << cs_arg->toString() << " \n");
                NodeID srcAA = cs_arg->getId();
                NodeID dstFA = fun_arg->getId();
                addCopyEdge(srcAA, dstFA);
            }
        }

        //Any remaining actual args must be varargs.
        if (F->isVarArg())
        {
            NodeID vaF = svfir->getVarargNode(F);
            DBOUT(DPAGBuild, outs() << "\n      varargs:");
            for (; csArgIt != csArgEit; ++csArgIt)
            {
                const PAGNode *cs_arg = *csArgIt;
                if (cs_arg->isPointer())
                {
                    NodeID vnAA = cs_arg->getId();
                    addCopyEdge(vnAA,vaF);
                }
            }
        }
        if(csArgIt != csArgEit)
        {
            writeWrnMsg("too many args to non-vararg func.");
            writeWrnMsg("(" + getSourceLoc(cs.getInstruction()) + ")");
        }
    }
}

/*!
 * Update call graph for the input indirect callsites
 */
bool CFLAlias::updateCallGraph(const CallSiteToFunPtrMap& callsites)
{

    // double cgUpdateStart = stat->getClk();

    CallEdgeMap newEdges;
    onTheFlyCallGraphSolve(callsites,newEdges);
    for(CallEdgeMap::iterator it = newEdges.begin(), eit = newEdges.end(); it!=eit; ++it )
    {
        CallSite cs = SVFUtil::getLLVMCallSite(it->first->getCallSite());
        for(FunctionSet::iterator cit = it->second.begin(), ecit = it->second.end(); cit!=ecit; ++cit)
        {
            std::cout << cs.arg_size();
            connectCaller2CalleeParams(cs,*cit);
        }
    }

    // double cgUpdateEnd = stat->getClk();
    //timeOfUpdateCallGraph += (cgUpdateEnd - cgUpdateStart) / TIMEINTERVAL;

    return (!solver->isWorklistEmpty());
}

void CFLAlias::analyze()
{
    GrammarBuilder grammarBuilder = GrammarBuilder(Options::GrammarFilename);
    CFGNormalizer normalizer = CFGNormalizer();
    AliasCFLGraphBuilder cflGraphBuilder = AliasCFLGraphBuilder();
    CFLGramGraphChecker cflChecker = CFLGramGraphChecker();
    if (Options::GraphIsFromDot == false)
    {
        PointerAnalysis::initialize();
        GrammarBase *grammarBase = grammarBuilder.build();
        ConstraintGraph *consCG = new ConstraintGraph(svfir);
        graph = cflGraphBuilder.buildBigraph(consCG, grammarBase->getStartKind(), grammarBase);
        cflChecker.check(grammarBase, &cflGraphBuilder, graph);
        grammar = normalizer.normalize(grammarBase);
        cflChecker.check(grammar, &cflGraphBuilder, graph);
        std::string svfirName = Options::InputFilename.c_str();
        svfir->dump(svfirName.append("_IR"));
        std::string grammarName = Options::InputFilename.c_str();
        grammar->dump(grammarName.append("_Grammar"));
        delete consCG;
        delete grammarBase;
    }
    else
    {
        GrammarBase *grammarBase = grammarBuilder.build();
        graph = cflGraphBuilder.buildFromDot(Options::InputFilename, grammarBase);
        cflChecker.check(grammarBase, &cflGraphBuilder, graph);
        grammar = normalizer.normalize(grammarBase);
        cflChecker.check(grammar, &cflGraphBuilder, graph);
        delete grammarBase;
    }
    solver = new CFLSolver(graph, grammar);
    bool reanalyze = false;
    //solver->pushIntoWorklist(graph->addCFLEdge(graph->getGNode(278),graph->getGNode(280), 1));
    //solver->pushIntoWorklist(graph->addCFLEdge(graph->getGNode(280),graph->getGNode(278), 12));
    do
    {
        solver->solve();
        if ( updateCallGraph(svfir->getIndirectCallsites()))
        {
            reanalyze = true;
        }
        else{
            reanalyze = false;
        }
        solver->reanalyze = reanalyze;
    }
    while (reanalyze);
    
    std::string CFLGraphFileName = Options::InputFilename.c_str();
    graph->dump(CFLGraphFileName.append("_CFL"));
    if (Options::GraphIsFromDot == false)
    {
        PointerAnalysis::finalize();
    }
}