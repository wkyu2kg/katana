/** Speculative ordered BFS -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2011, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Description
 *
 * Speculative ordered BFS.
 *
 * @author <ahassaan@ices.utexas.edu>
 */

#include <vector>
#include <functional>

#include "Galois/Runtime/OrderedSpeculation.h"

#include "bfs.h"
#include "bfsParallel.h"

using Level_ty = unsigned;
class SpecOrderedBFS: public BFS<Level_ty> {


  struct OpFunc {

    static const unsigned CHUNK_SIZE = DEFAULT_CHUNK_SIZE;

    Graph& graph;
    ParCounter& numIter;

    OpFunc (Graph& graph, ParCounter& numIter): graph (graph), numIter (numIter) {}

    template <typename C>
    void operator () (const Update& up, C& ctx) {

      if (graph.getData (up.node, Galois::MethodFlag::UNPROTECTED) == BFS_LEVEL_INFINITY) {

        graph.getData (up.node, Galois::MethodFlag::UNPROTECTED) = up.level;

        auto undo = [this, up] (void) {
          graph.getData (up.node, Galois::MethodFlag::UNPROTECTED) = BFS_LEVEL_INFINITY;
        };

        ctx.addUndoAction (undo);


        for (typename Graph::edge_iterator ni = graph.edge_begin (up.node, Galois::MethodFlag::UNPROTECTED)
            , eni = graph.edge_end (up.node, Galois::MethodFlag::UNPROTECTED); ni != eni; ++ni) {

          GNode dst = graph.getEdgeDst (ni);
          ctx.push (Update (dst, up.level + 1));

          // if (graph.getData (dst, Galois::MethodFlag::UNPROTECTED) == BFS_LEVEL_INFINITY) {
            // ctx.push (Update (dst, up.level + 1));
          // }
        }

      }

      auto inc = [this] (void) {
        numIter += 1;
      };

      ctx.addCommitAction (inc);
    }

  };

public:

  virtual const std::string getVersion () const { return "Speculative ordered"; }

  virtual size_t runBFS (Graph& graph, GNode& startNode) {

    ParCounter numIter;


    // update request for root
    Update first (startNode, 0);

    std::vector<Update> wl;
    wl.push_back (first);

    Galois::Runtime::for_each_ordered_spec (
        Galois::Runtime::makeStandardRange(wl.begin (), wl.end ()),
        Comparator (), 
        VisitNhood (graph),
        OpFunc (graph, numIter),
        std::make_tuple (Galois::loopname ("bfs-speculative")));


    std::cout << "number of iterations: " << numIter.reduce () << std::endl;


    return numIter.reduce ();
  }


};

int main (int argc, char* argv[]) {
  SpecOrderedBFS wf;
  wf.run (argc, argv);
  return 0;
}
