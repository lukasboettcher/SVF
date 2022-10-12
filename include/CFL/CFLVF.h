//===----- CFLVF.h -- CFL Value-Flow Client--------------//
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
 * CFLVF.h
 *
 *  Created on: September 5, 2022
 *      Author: Pei Xu
 */

#ifndef INCLUDE_CFL_CFLVF_H_
#define INCLUDE_CFL_CFLVF_H_

#include "CFL/CFLStat.h"
#include "SABER/SaberSVFGBuilder.h"
#include "WPA/Andersen.h"

namespace SVF
{

class CFLVF : public BVDataPTAImpl
{

public:
    CFLVF(SVFIR* ir) : BVDataPTAImpl(ir, PointerAnalysis::CFLFSCS_WPA, false), svfir(ir), graph(nullptr), grammar(nullptr), solver(nullptr)
    {
    }

    /// Initialize the grammar, graph, solver
    virtual void initialize();

    /// Print grammar and graph
    virtual void finalize();

    /// Destructor
    virtual ~CFLVF()
    {
        delete solver;
    }

    /// Start Analysis here (main part of pointer analysis).
    virtual void analyze();

    /// Get CFL graph
    CFLGraph* getCFLGraph()
    {
        return graph;
    }

private:
    SVFIR* svfir;
    CFLGraph* graph;
    CFLGrammar* grammar;
    CFLSolver *solver;
    SaberSVFGBuilder memSSA;
    SVFG* svfg;
};

} // End namespace SVF

#endif /* INCLUDE_CFL_CFLVF_H_*/
