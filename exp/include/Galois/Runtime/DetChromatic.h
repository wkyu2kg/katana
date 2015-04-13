#ifndef GALOIS_RUNTIME_DET_CHROMATIC_H
#define GALOIS_RUNTIME_DET_CHROMATIC_H

#include "Galois/Accumulator.h"
#include "Galois/AltBag.h"
#include "Galois/DoAllWrap.h"
#include "Galois/Galois.h"
#include "Galois/Atomic.h"
//#include "Galois/GaloisUnsafe.h"

#include "Galois/Graph/Graph.h"

#include "Galois/WorkList/WorkListWrapper.h"
#include "Galois/WorkList/ExternalReference.h"

#include <atomic>
#include <vector>

namespace Galois {
namespace Runtime {

enum class InputDAG_ExecTy {
  CHROMATIC,
  EDGE_FLIP,
  TOPO,
  PART,
};

namespace cll = llvm::cl;

static cll::opt<InputDAG_ExecTy> inputDAG_ExecTy (
    "executor",
    cll::desc ("Deterministic Executor Type"),
    cll::values (
      clEnumValN (InputDAG_ExecTy::CHROMATIC, "InputDAG_ExecTy::CHROMATIC", "Chromatic Executor"),
      clEnumValN (InputDAG_ExecTy::EDGE_FLIP, "InputDAG_ExecTy::EDGE_FLIP", "Edge Flipping DAG overlayed on input graph"),
      clEnumValN (InputDAG_ExecTy::TOPO, "InputDAG_ExecTy::TOPO", "Edge Flipping DAG overlayed on input graph"),
      clEnumValN (InputDAG_ExecTy::PART, "InputDAG_ExecTy::PART", "Partitioned coarsened DAG overlayed on input graph"),
      clEnumValEnd),
    cll::init (InputDAG_ExecTy::CHROMATIC));


enum class PriorityFunc {
  FIRST_FIT,
  BY_ID,
  RANDOM,
  MIN_DEGREE,
  MAX_DEGREE,
};


static cll::opt<PriorityFunc> priorityFunc (
    "priority",
    cll::desc ("choose ordering heuristic"),
    cll::values (
      clEnumValN (PriorityFunc::FIRST_FIT, "PriorityFunc::FIRST_FIT", "first fit, no priority"),
      clEnumValN (PriorityFunc::BY_ID, "PriorityFunc::BY_ID", "order by ID modulo some constant"),
      clEnumValN (PriorityFunc::RANDOM, "PriorityFunc::RANDOM", "uniform random within some small range"),
      clEnumValN (PriorityFunc::MIN_DEGREE, "PriorityFunc::MIN_DEGREE", "order by min degree first"),
      clEnumValN (PriorityFunc::MAX_DEGREE, "PriorityFunc::MAX_DEGREE", "order by max degree first"),
      clEnumValEnd),
    cll::init (PriorityFunc::BY_ID));

struct BaseDAGdata {
  // std::atomic<unsigned> onWL;
  GAtomic<int> onWL;
  // std::atomic<unsigned> indegree;
  GAtomic<int> indegree;
  int indeg_backup;

  unsigned id;
  unsigned priority;
  int color;

  explicit BaseDAGdata (unsigned id) : 
    onWL (0),
    indegree (0),
    indeg_backup (0),
    id (id), 
    priority (0), 
    color (-1)
  {}
};


template <typename ND>
struct DAGdataComparator {

  static int compare3val (const ND& left, const ND& right) {
    int r = left.priority - right.priority;
    if (r != 0) { 
      return r;
    } else {
      return (r = left.id - right.id);
    }
  }

  static bool compare (const ND& left, const ND& right) {
    if (left.priority != right.priority) {
      return left.priority < right.priority;
    } else {
      return left.id < right.id;
    }
  }

  bool operator () (const ND& left, const ND& right) const {
    return compare (left, right);
  }
};

struct InputDAGdata: public BaseDAGdata {


  unsigned numSucc;
  unsigned* dagSucc;
  


  explicit InputDAGdata (unsigned id=0): 
    BaseDAGdata (id),
    numSucc (0), 
    dagSucc (nullptr)
  {}

  struct VisitDAGsuccessors {

    template <typename GNode, typename ND, typename F>
    void operator () (GNode src, ND& sd, F& f) {

      for (unsigned i = 0; i < sd.numSucc; ++i) {
        GNode dst = sd.dagSucc[i];
        f (dst);
      }
    }
  };

};

struct InputDAGdataInOut: public BaseDAGdata {


  // offset where dag successors end and predecessors begin
  ptrdiff_t dagSuccEndIn;
  ptrdiff_t dagSuccEndOut;
  

  explicit InputDAGdataInOut (unsigned id=0): 
    BaseDAGdata (id),
    dagSuccEndIn (0), 
    dagSuccEndOut (0)
  {}

};

struct InputDAGdataDirected: public InputDAGdata {

  // TODO: change to vector with pow 2 alloc
  typedef Galois::gdeque<unsigned, 64> AdjList;

  AdjList incoming;

  InputDAGdataDirected (unsigned id): InputDAGdata (id) {}

  void addIncoming (unsigned n) {
    assert (std::find (incoming.begin (), incoming.end (), n) == incoming.end ());
    incoming.push_back (n);
  }
  
};


struct TaskDAGdata: public BaseDAGdata {

  SimpleRuntimeContext* taskCtxt;

  explicit TaskDAGdata (unsigned id=0): 
    BaseDAGdata (id),
    taskCtxt (nullptr)
  {}

};


template <typename G, typename A, typename D>
struct DAGmanagerBase {

protected:
  static const bool DEBUG = false;

  static const unsigned DEFAULT_CHUNK_SIZE = 4;

  typedef typename G::GraphNode GNode;
  typedef typename G::node_data_type ND;

  typedef Galois::PerThreadVector<bool> PerThrdColorVec;

  G& graph;
  A visitAdj;
  D visitDAGsucc;
  PerThrdColorVec perThrdColorVec;
  Galois::GReduceMax<int> maxColors;

  DAGmanagerBase (G& graph, const A& visitAdj, const D& visitDAGsucc=D())
    : graph (graph), visitAdj (visitAdj), visitDAGsucc (visitDAGsucc)
  {
    // // mark 0-th color as taken
    // for (unsigned i = 0; i < perThrdColorVec.numRows (); ++i) {
      // auto& forbiddenColors = perThrdColorVec.get(i);
      // forbiddenColors.resize (1, 0);
      // forbiddenColors[0] = 0;
    // }

    if (DEBUG) {
      fprintf (stderr, "WARNING: DAGmanagerBase DEBUG mode on, timing may be off\n");
    }
  }


public:

  template <typename F>
  void applyToAdj (GNode src, F& f, const Galois::MethodFlag& flag=Galois::MethodFlag::UNPROTECTED) {
    visitAdj (src, f, flag);
  }

  template <typename F>
  void applyToDAGsucc (GNode src, ND& srcData, F& f) {
    visitDAGsucc (src, srcData, f);
  }

  template <typename P>
  void initDAG (P postInit) {

    assignPriority ();

    Galois::do_all_choice (
        Galois::Runtime::makeLocalRange (graph),
        [this, &postInit] (GNode src) {
          
          auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);

          assert (sd.indegree == 0);
          int indeg = 0;

          auto countDegClosure = [this, &sd, &indeg] (GNode dst) {
            auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);

            int c = DAGdataComparator<ND>::compare3val (dd, sd);
            if (c < 0) { // dd < sd
              ++indeg;
            } 
          };

          applyToAdj (src, countDegClosure);

          sd.indegree = indeg;
          sd.indeg_backup = sd.indegree;


          postInit (graph, src, sd);

        },
        "init-DAG",
        Galois::doall_chunk_size<DEFAULT_CHUNK_SIZE> ());
  }



  template <typename R, typename W>
  void reinitActiveDAG (const R& range, W& sources) {

    // XXX: may be superfluous
    Galois::do_all_choice (
        range,
        [this] (GNode src) {
          auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
          assert (sd.onWL > 0);
          sd.indegree = 0;
        },
        "reinitActiveDAG-0",
        Galois::doall_chunk_size<DEFAULT_CHUNK_SIZE> ());

    Galois::do_all_choice (
        range,
        [this] (GNode src) {

          auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
          assert (sd.onWL > 0);

          auto closure = [this] (GNode dst) {
            auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);
            if (dd.onWL > 0) {
              ++(dd.indegree);
            }
          };

          applyToDAGsucc (src, sd, closure);
        },
        "reinitActiveDAG-1",
        Galois::doall_chunk_size<DEFAULT_CHUNK_SIZE> ());

    Galois::do_all_choice (
        range,
        [this, &sources] (GNode src) {
          auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
          assert (sd.onWL > 0);
          if (sd.indegree == 0) {
            sources.push (src);

            // if (DEBUG) {
              // auto closure = [this] (GNode dst) {
                // auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);
                // if (dd.onWL > 0) {
                  // assert (int (dd.indegree) > 0);
                // }
              // };
// 
              // applyToDAGsucc (src, sd, closure);
            // }
          }
        },
        "reinitActiveDAG-2",
        Galois::doall_chunk_size<DEFAULT_CHUNK_SIZE> ());

    // Galois::do_all_choice (
        // range,
        // [this, &sources] (GNode src) {
          // 
          // auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
          // assert (sd.onWL > 0);
// 
          // // if (sd.onWL > 0) {
            // sd.indegree = 0; // reset
// 
            // unsigned addAmt = 0;
// 
            // auto closure = [this, &sd, &addAmt] (GNode dst) {
              // auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);
// 
              // if (dd.onWL > 0) && DAGdataComparator<ND>::compare (dd, sd)) { // dd < sd
                // ++addAmt;
              // }
            // };
// 
// 
            // applyToAdj (src, closure);
// 
            // sd.indegree += addAmt;
// 
            // if (addAmt == 0) {
              // assert (sd.indegree == 0);
              // assert (sd.onWL > 0);
              // sources.push (src);
            // }
            // sd.indeg_backup = sd.indegree; 
          // // } // end if
// 
        // },
        // "reinit-Active-DAG",
        // Galois::doall_chunk_size<DEFAULT_CHUNK_SIZE> ());
  }

  
  template <typename W>
  void reinitDAG (W& sources) {

    Galois::do_all_choice ( Galois::Runtime::makeLocalRange (graph), 
        [this, &sources] (GNode src) {
          auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
          sd.indegree = sd.indeg_backup;

          if (sd.indegree == 0) {
            sources.push (src);
          }
        },
        "reinitDAG",
        Galois::doall_chunk_size<DEFAULT_CHUNK_SIZE> ());

  }

  struct FakeBag {
    void push (const GNode&) const {}
  };

  void reinitDAG (void) {
    FakeBag b;
    reinitDAG (b);
  }

  template <typename W>
  void collectSources (W& sources) {
    Galois::do_all_choice ( Galois::Runtime::makeLocalRange (graph), 
        [this, &sources] (GNode src) {
          auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
          // assert (sd.indegree == sd.indeg_backup);
          if (sd.indegree == 0) {
            sources.push (src);
          }
        }, 
        "collect-sources",
        Galois::doall_chunk_size<DEFAULT_CHUNK_SIZE> ());
  }

  template <typename F>
  struct RunDAGcomp {
    typedef int tt_does_not_need_aborts;
    DAGmanagerBase& outer;
    F& func;

    template <typename C>
    void operator () (GNode src, C& ctx) {

      auto& sd = outer.graph.getData (src, Galois::MethodFlag::UNPROTECTED);
      assert (sd.indegree == 0);

      func (src);

      auto dagClosure = [this, &ctx] (GNode dst) {
        auto& dd = outer.graph.getData (dst, Galois::MethodFlag::UNPROTECTED);

        int x = --(dd.indegree);
        // assert (x >= 0); // FIXME
        if (x == 0) { 
          // color == 0 is uncolored
          // assert (dd.color == 0); // FIXME
          ctx.push (dst);
        }
      };

      outer.applyToDAGsucc (src, sd, dagClosure);

    }
  };

  template <typename F, typename W>
  void runDAGcomputation (F func, W& sources, const char* loopname) {

    Galois::StatTimer t (loopname);

    t.start ();

    using WL_ty =  Galois::WorkList::AltChunkedFIFO<DEFAULT_CHUNK_SIZE>;

    Galois::for_each_local(
        sources,
        RunDAGcomp<F> {*this, func},
        Galois::loopname (loopname),
        Galois::wl<WL_ty> ());


    t.stop ();
  }

  template <typename F>
  void runDAGcomputation (F func, const char* loopname) {
    Galois::InsertBag<GNode> sources;

    collectSources (sources);
    runDAGcomputation (func, sources, loopname);
  }

  void assignIDs (void) {

    const size_t numNodes = graph.size ();
    Galois::on_each (
        [&] (const unsigned tid, const unsigned numT) {

          size_t num_per = (numNodes + numT - 1) / numT;
          size_t beg = tid * num_per;
          size_t end = std::min (numNodes, (tid + 1) * num_per);

          auto it_beg = graph.begin ();
          std::advance (it_beg, beg);

          auto it_end = it_beg; 
          std::advance (it_end, (end - beg));

          for (; it_beg != it_end; ++it_beg) {
            auto& nd = graph.getData (*it_beg, Galois::MethodFlag::UNPROTECTED);
            nd.id = beg++;
          }
        },
        Galois::loopname ("assign-ids"));
  }


  template <typename F>
  void assignPriorityHelper (const F& nodeFunc) {
    Galois::do_all_choice (
        Galois::Runtime::makeLocalRange (graph),
        [&] (GNode node) {
          nodeFunc (node);
        },
        "assign-priority",
        Galois::doall_chunk_size<DEFAULT_CHUNK_SIZE> ());
  }

  static const unsigned MAX_LEVELS = 100;
  static const unsigned SEED = 10;

  struct RNG {
    std::uniform_int_distribution<unsigned> dist;
    std::mt19937 eng;
    
    RNG (void): dist (0, MAX_LEVELS), eng () {
      this->eng.seed (SEED);
    }

    unsigned operator () (void) {
      return dist(eng);
    }
  };

  void assignPriority (void) {
    assignIDs ();
    auto byId = [&] (GNode node) {
      auto& nd = graph.getData (node, Galois::MethodFlag::UNPROTECTED);
      nd.priority = nd.id % MAX_LEVELS;
    };

    Galois::Runtime::PerThreadStorage<RNG>  perThrdRNG;

    // TODO: non-deterministic at the moment
    // can be fixed by making thread K call the generator
    // N times, where N is sum of calls of all threads < K
    auto randPri = [&] (GNode node) {
      auto& rng = *(perThrdRNG.getLocal ());
      auto& nd = graph.getData (node, Galois::MethodFlag::UNPROTECTED);
      nd.priority = rng ();
    };


    auto minDegree = [&] (GNode node) {
      auto& nd = graph.getData (node, Galois::MethodFlag::UNPROTECTED);
      nd.priority = std::distance (
                      graph.edge_begin (node, Galois::MethodFlag::UNPROTECTED),
                      graph.edge_end (node, Galois::MethodFlag::UNPROTECTED));
    };

    const size_t numNodes = graph.size ();
    auto maxDegree = [&] (GNode node) {
      auto& nd = graph.getData (node, Galois::MethodFlag::UNPROTECTED);
      nd.priority = numNodes - std::distance (
                                  graph.edge_begin (node, Galois::MethodFlag::UNPROTECTED),
                                  graph.edge_end (node, Galois::MethodFlag::UNPROTECTED));
    };
    
    Galois::StatTimer t_priority ("priority assignment time: ");

    t_priority.start ();

    switch (priorityFunc) {
      case PriorityFunc::FIRST_FIT:
        // do nothing
        break;

      case PriorityFunc::BY_ID:
        assignPriorityHelper (byId);
        break;

      case PriorityFunc::RANDOM:
        assignPriorityHelper (randPri);
        break;

      case PriorityFunc::MIN_DEGREE:
        assignPriorityHelper (minDegree);
        break;

      case PriorityFunc::MAX_DEGREE:
        assignPriorityHelper (maxDegree);
        break;

      default:
        std::abort ();
    }

    t_priority.stop ();
  }


  void colorNode (GNode src) {
    auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
    assert (sd.indegree == 0);
    assert (sd.color == -1); // uncolored

    auto& forbiddenColors = perThrdColorVec.get ();
    std::fill (forbiddenColors.begin (), forbiddenColors.end (), false);


    auto colorClosure = [this, &forbiddenColors, &sd] (GNode dst) {
      auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);

      if (int(forbiddenColors.size ()) <= dd.color) {
        forbiddenColors.resize (dd.color + 1, false);
      }
      // std::printf ("Neighbor %d has color %d\n", dd.id, dd.color);

      forbiddenColors[dd.color] = true;

    };

    applyToAdj (src, colorClosure);

    for (size_t i = 0; i < forbiddenColors.size (); ++i) {
      if (!forbiddenColors[i]) { 
        sd.color = i;
        break;
      }
    }

    if (sd.color == -1) {
      sd.color = forbiddenColors.size ();
    }
    maxColors.update (sd.color);


    // std::printf ("Node %d assigned color %d\n", sd.id, sd.color);

  }


  void colorDAG (void) {

    auto func = [this] (GNode src) { this->colorNode (src); };
    runDAGcomputation (func, "color-DAG");
    std::printf ("DAG colored with %d colors\n", maxColors.reduceRO ());
  }

  unsigned getMaxColors (void) const {
    return maxColors.reduceRO ();
  }

};

template <typename G>
struct DAGmanagerInOut {
  typedef typename G::GraphNode GNode;

  struct VisitAdjacent {
    G& graph;

    template <typename F>
    void operator () (GNode src, F& func, const Galois::MethodFlag& flag) {

      for (auto i = graph.in_edge_begin (src, flag)
          , end_i = graph.in_edge_end (src, flag); i != end_i; ++i) {
        GNode dst = graph.getInEdgeDst (i);
        func (dst);
      }

      for (auto i = graph.edge_begin (src, flag)
          , end_i = graph.edge_end (src, flag); i != end_i; ++i) {
        GNode dst = graph.getEdgeDst (i);
        func (dst);
      }
    }
  };

  struct VisitDAGsuccessors {
    G& graph;

    template <typename ND, typename F>
    void operator () (GNode src, ND& sd, F& func) {

      for (auto i = graph.in_edge_begin (src, Galois::MethodFlag::UNPROTECTED)
          , end_i = i + sd.dagSuccEndIn; i != end_i; ++i) {

        assert (i <= end_i);
        GNode dst = graph.getInEdgeDst (i);
        func (dst);
      }

      for (auto i = graph.edge_begin (src, Galois::MethodFlag::UNPROTECTED)
          , end_i = i + sd.dagSuccEndOut; i != end_i; ++i) {

        assert (i <= end_i);
        GNode dst = graph.getEdgeDst (i);
        func (dst);
      }
    }

  };

  struct VisitDAGpredecessors {
    G& graph;

    template <typename ND, typename F>
    void operator () (GNode src, ND& sd, F& func) {

      for (auto i = graph.in_edge_begin (src, MethodFlag::UNPROTECTED) + sd.dagSuccEndIn
          , end_i = graph.in_edge_end (src, MethodFlag::UNPROTECTED); i != end_i; ++i) {

        assert (i <= end_i);
        GNode dst = graph.getInEdgeDst (i);
        func (dst);
      }

      for (auto i = graph.edge_begin (src, MethodFlag::UNPROTECTED) + sd.dagSuccEndOut
          , end_i = graph.edge_end (src, MethodFlag::UNPROTECTED); i != end_i; ++i) {

        assert (i <= end_i);
        GNode dst = graph.getEdgeDst (i);
        func (dst);
      }
    }
  };

  template <typename ND>
  struct Predicate {
    G& graph;
    const ND& srcData;

    bool operator () (GNode dst) const {
      const auto& dstData = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);
      return DAGdataComparator<ND>::compare3val (srcData, dstData) < 0;
    }
  };

  struct InitDAGoffsets {


    template <typename ND>
    void operator () (G& graph, GNode src, ND& sd) {

      Predicate<ND> pred {graph, sd};

      ptrdiff_t out_off = graph.partition_neighbors (src, pred);
      ptrdiff_t in_off = graph.partition_in_neighbors (src, pred);

      sd.dagSuccEndOut = out_off;
      sd.dagSuccEndIn = in_off;

      static const bool VERIFY = false;

      if (VERIFY) {

        auto funcSucc = [&pred] (GNode dst) {
          assert (pred (dst));
        };

        VisitDAGsuccessors visitDAGsucc {graph};

        visitDAGsucc (src, sd, funcSucc);

        auto funcPred = [&pred] (GNode dst) {
          assert (!pred (dst));
        };

        VisitDAGpredecessors visitDAGpred{graph};
        visitDAGpred (src, sd, funcPred);

      }
    }
  };

  typedef DAGmanagerBase<G, VisitAdjacent, VisitDAGsuccessors> Base_ty;

  struct Manager: public Base_ty {

    Manager (G& graph): Base_ty {graph, VisitAdjacent {graph}, VisitDAGsuccessors {graph}}
    {}

    void initDAG (void) {
      Base_ty::initDAG (InitDAGoffsets ());
    }

  };

};

template <typename G, typename A>
struct DAGmanagerDefault: public DAGmanagerBase<G, A, InputDAGdata::VisitDAGsuccessors> {

  using Base = DAGmanagerBase<G, A, InputDAGdata::VisitDAGsuccessors>;
  using GNode = typename G::GraphNode;
  using ND = typename G::node_data_type;

  Galois::Runtime::MM::Pow_2_BlockAllocator<unsigned> dagSuccAlloc;

  DAGmanagerDefault (G& graph, const A& visitAdj)
    : Base (graph, visitAdj, InputDAGdata::VisitDAGsuccessors ()) 
  {}

  void initDAG (void) {

    auto postInit = [this] (G& graph, GNode src, ND& sd) {

      unsigned outdeg = 0;

      auto countDegClosure = [&graph, &sd, &outdeg] (GNode dst) {
        auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);

        int c = DAGdataComparator<ND>::compare3val (dd, sd);
        if (c > 0) { // sd < dd
          ++outdeg;
        }
      };

      Base::applyToAdj (src, countDegClosure);

      sd.numSucc = outdeg;
      sd.dagSucc = dagSuccAlloc.allocate (sd.numSucc);
      assert (sd.dagSucc != nullptr);

      unsigned i = 0;
      auto fillDAGsucc = [&graph, &sd, &i] (GNode dst) {
        auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);

        int c = DAGdataComparator<ND>::compare3val (dd, sd);
        if (c > 0) { // dd > sd
          sd.dagSucc[i++] = dst;
        }
      };

      Base::applyToAdj (src, fillDAGsucc);
      assert (i == sd.numSucc);
    };

    Base::initDAG (postInit);
  }

  void freeDAGdata (void) {
    Galois::do_all_choice ( Galois::Runtime::makeLocalRange (Base::graph), 
        [this] (GNode src) {
          auto& sd = Base::graph.getData (src, Galois::MethodFlag::UNPROTECTED);
          dagSuccAlloc.deallocate (sd.dagSucc, sd.numSucc);
          sd.numSucc = 0;
          sd.dagSucc = nullptr;
        },
        "freeDAGdata",
        Galois::doall_chunk_size<Base::DEFAULT_CHUNK_SIZE> ());

  }

  ~DAGmanagerDefault (void) {
    freeDAGdata ();
  }

};






template <typename G>
struct DAGvisitorUndirected {

  typedef typename G::GraphNode GNode;

  struct VisitAdjacent {
    G& graph;

    template <typename F>
    void operator () (GNode src, F& func, const Galois::MethodFlag& flag) {

      for (auto i = graph.edge_begin (src, flag)
           , end_i = graph.edge_end (src, flag); i != end_i; ++i) {

        GNode dst = graph.getEdgeDst (i);
        func (dst);
      }
    }
  };

};


// TODO: complete implementation
//
// template <typename G>
// struct DAGvisitorDirected {
// 
  // void addPredecessors (void) {
// 
    // Galois::do_all_choice (
        // Galois::Runtime::makeLocalRange (graph),
        // [this] (GNode src) {
        // 
          // auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
          // 
          // unsigned addAmt = 0;
          // for (Graph::edge_iterator e = graph.edge_begin (src, Galois::MethodFlag::UNPROTECTED),
              // e_end = graph.edge_end (src, Galois::MethodFlag::UNPROTECTED); e != e_end; ++e) {
            // GNode dst = graph.getEdgeDst (e);
            // auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);
// 
            // if (src != dst) {
              // dd.addPred (src);
            // }
          // }
// 
        // },
        // "initDAG",
        // Galois::doall_chunk_size<DEFAULT_CHUNK_SIZE> ());
// 
  // }
// 
// };



template <typename G, typename F>
struct ChromaticExecutor {

  typedef typename G::GraphNode GNode;

  static const unsigned CHUNK_SIZE = F::CHUNK_SIZE;
  typedef Galois::WorkList::AltChunkedFIFO<CHUNK_SIZE, GNode> Inner_WL_ty;
  typedef Galois::WorkList::WLsizeWrapper<Inner_WL_ty> WL_ty;
  typedef PerThreadStorage<UserContextAccess<GNode> > PerThreadUserCtx;

  G& graph;
  F func;
  const char* loopname;
  unsigned nextIndex;

  std::vector<WL_ty*> colorWorkLists;
  PerThreadUserCtx userContexts;

  ChromaticExecutor (G& graph, const F& func, unsigned maxColors, const char* loopname)
    : graph (graph), func (func), loopname (loopname), nextIndex (0) {
    
      assert (maxColors > 0);
      colorWorkLists.resize (maxColors, nullptr);
      
      for (unsigned i = 0; i < maxColors; ++i) {
        colorWorkLists[i] = new WL_ty ();
      }
  }

  ~ChromaticExecutor (void) {
    for (unsigned i = 0; i < colorWorkLists.size (); ++i) {
      delete colorWorkLists[i];
      colorWorkLists[i] = nullptr;
    }
  }

  void push (GNode n) {
    auto& data = graph.getData (n);

    unsigned i = data.color;
    assert (i < colorWorkLists.size ());

    int expected = 0;
    if (data.onWL.cas (expected, 1)) {
      colorWorkLists[i]->push (n);
    }
  }

  WL_ty* chooseLargest (void) {
    WL_ty* nextWL = nullptr;

    unsigned maxSize = 0;
    for (unsigned i = 0; i < colorWorkLists.size (); ++i) {

      size_t s = colorWorkLists[i]->size ();
      if (s > 0 && s > maxSize) {
        maxSize = s;
        nextWL = colorWorkLists[i];
      }
    }

    return nextWL;
  }

  WL_ty* chooseFirst (void) {
    WL_ty* nextWL = nullptr;

    for (unsigned i = 0; i < colorWorkLists.size (); ++i) {
      if (colorWorkLists[i]->size () > 0) {
        nextWL = colorWorkLists[i];
        break;
      }
    }

    return nextWL;
  }

  WL_ty* chooseNext (void) {
    WL_ty* nextWL = nullptr;

    for (unsigned i = 0; i < colorWorkLists.size (); ++i) {

      unsigned j = (nextIndex + i) % colorWorkLists.size ();
      size_t s = colorWorkLists[j]->size ();
      if (s > 0) {
        nextWL = colorWorkLists[j];
        nextIndex = (j + 1) % colorWorkLists.size ();
        break;
      }
    }

    return nextWL;
  }

  struct ApplyOperator {
    typedef int tt_does_not_need_aborts;
    ChromaticExecutor& outer;

    template <typename C>
    void operator () (GNode n, C& ctx) {

      // auto& userCtx = *(outer.userContexts.getLocal ());
// 
      // userCtx.reset ();
      // outer.func (n, userCtx);
      auto& nd = outer.graph.getData (n, Galois::MethodFlag::UNPROTECTED);
      nd.onWL = 0;
      outer.func (n, outer);


      // for (auto i = userCtx.getPushBuffer ().begin (), 
          // end_i = userCtx.getPushBuffer ().end (); i != end_i; ++i) {
// 
        // outer.push (*i);
      // }
    }
  };


  template <typename R>
  void execute (const R& range) {

    // fill initial
    do_all_impl (range,
        [this] (GNode n) {
          push (n);
        },
        "fill_initial",
        false );

    unsigned rounds = 0;
    // process until done
    while (true) {

      // find non-empty WL
      // TODO: cmd line option to choose
      // the order in which worklists are picked
      WL_ty* nextWL = chooseNext ();

      if (nextWL == nullptr) {
        break;
        // double check that all WL are empty
      }

      ++rounds;

      // run for_each
      typedef Galois::WorkList::ExternalReference<WL_ty> WL;
      GNode* it = 0;

      for_each(it, it,
          ApplyOperator {*this},
          Galois::loopname(loopname),
          Galois::wl<WL>(nextWL));

      nextWL->reset_all ();
    }

    std::printf ("ChromaticExecutor: performed %d rounds\n", rounds);

  }

};

template <typename R, typename F, typename G, typename M>
void for_each_det_chromatic (const R& range, const F& func, G& graph, M& dagManager, const char* loopname) {

  Galois::Runtime::getSystemThreadPool ().burnPower (Galois::getActiveThreads ());

  dagManager.colorDAG ();

  ChromaticExecutor<G,F> executor {graph, func, dagManager.getMaxColors (), loopname};

  executor.execute (range);

  Galois::Runtime::getSystemThreadPool ().beKind ();

}

// TODO: logic to choose correct DAG type based on graph type or some graph tag
template <typename R, typename G, typename F>
void for_each_det_chromatic (const R& range, const F& func, G& graph, const char* loopname) {

  typedef typename DAGmanagerInOut<G>::Manager  M;
  M dagManager {graph};

  for_each_det_chromatic (range, func, graph, 
      dagManager, loopname);

}


template <typename G, typename M, typename F>
struct ChromaticReuseExecutor {

  typedef typename G::GraphNode GNode;

  static const unsigned CHUNK_SIZE = F::CHUNK_SIZE;
  typedef Galois::WorkList::AltChunkedFIFO<CHUNK_SIZE, GNode> WL_ty;
  typedef Galois::PerThreadBag<GNode> Bag_ty;
  
  G& graph;
  M& dagManager;
  F func;

  std::string loopname;

  std::vector<Bag_ty*> colorBags;

  ChromaticReuseExecutor (G& graph, M& dagManager, const F& func, const char* loopname) :
    graph {graph},
    dagManager {dagManager},
    func (func),
    loopname {loopname}
  {}


  void push_initial (GNode n) {
    auto& data = graph.getData (n, Galois::MethodFlag::UNPROTECTED);

    unsigned i = data.color;
    assert (i < colorBags.size ());

    int expected = 0;
    if (data.onWL.cas (expected, 1)) {
      colorBags[i]->push (n);
    }
  }

  template <typename R>
  void initialize (const R& range) {
    StatTimer t_init ("ChromaticReuseExecutor: coloring and bucket initialization time:");

    t_init.start ();

    dagManager.colorDAG ();

    unsigned maxColors = dagManager.getMaxColors ();

    assert (colorBags.size () == 0);
    colorBags.resize (maxColors, nullptr);

    for (unsigned i = 0; i < maxColors; ++i) {
      assert (colorBags[i] == nullptr);
      colorBags[i] = new Bag_ty ();
    }


    Galois::do_all_choice (
        range,
        [this] (GNode node) {
          push_initial (node);
        }, 
        "push_initial",
        Galois::doall_chunk_size<CHUNK_SIZE> ());

    t_init.stop ();
  }

  void push (GNode n) {
    GALOIS_DIE ("push not supported");
  }

  struct ApplyOperator {

    ChromaticReuseExecutor& outer;

    void operator () (GNode n) {
      outer.func (n, outer);
    }

  };


  void execute (void) {


    StatTimer t_exec ("ChromaticReuseExecutor: execution time:");

    t_exec.start ();

    for (auto i = colorBags.begin (), end_i = colorBags.end ();
        i != end_i; ++i) {

      assert (*i != nullptr);

      Galois::do_all_choice (makeLocalRange (**i),
          ApplyOperator {*this},
          loopname,
          Galois::doall_chunk_size<CHUNK_SIZE> ());

    }

    t_exec.stop ();
    

  }

  void reinitDAG (void) const {}

};


template <typename G, typename M, typename F>
struct InputGraphDAGreuseExecutor {

  typedef typename G::GraphNode GNode;

  typedef Galois::PerThreadBag<GNode> Bag_ty;

  static const unsigned CHUNK_SIZE = F::CHUNK_SIZE;
  typedef Galois::WorkList::AltChunkedFIFO<CHUNK_SIZE, GNode> WL_ty;


  G& graph;
  M& dagManager;
  F func;
  std::string loopname;

  Bag_ty initialSources;

  InputGraphDAGreuseExecutor (G& graph, M& dagManager, const F& func, const char* loopname)
    :
      graph (graph),
      dagManager (dagManager),
      func (func),
      loopname (loopname)
  {}

  template <typename R>
  void push_initial (const R& range) {

    do_all_choice (
        range,
        [this] (GNode node) {
          auto& sd = graph.getData (node, Galois::MethodFlag::UNPROTECTED);
          sd.onWL = 1;
        },
        "push_initial",
        Galois::doall_chunk_size<CHUNK_SIZE> ());

  }

  // assumes all nodes are active
  void initialize (void) {
    StatTimer t_init ("InputGraphDAGreuseExecutor: initialization time:");

    t_init.start ();

    push_initial (makeLocalRange (graph));

    dagManager.initDAG ();
    dagManager.collectSources (initialSources);

    t_init.stop ();
  }

  template <typename R>
  void initialize (const R& range) {
    StatTimer t_init ("InputGraphDAGreuseExecutor: initialization time:");

    t_init.start ();

    push_initial (range);

    dagManager.initDAG ();

    dagManager.reinitActiveDAG (range, initialSources);

    t_init.stop ();
  }

  void push (GNode n) {
    GALOIS_DIE ("push not supported");
  }


  void execute (void) {

    auto f = [this] (GNode src) { this->func (src, *this); };
    dagManager.runDAGcomputation (f, initialSources, loopname);
  }

  void reinitDAG (void) {
    dagManager.reinitDAG ();
  }

  template <typename R> 
  void reinitActiveDAG (const R& range) {

    initialSources.clear_all_parallel ();
    dagManager.reinitActiveDAG (range, initialSources);
  }

};

template <typename G, typename F, typename M>
struct InputGraphDAGexecutor {

  typedef typename G::GraphNode GNode;

  typedef Galois::PerThreadBag<GNode> Bag_ty;

  static const unsigned CHUNK_SIZE = F::CHUNK_SIZE;
  typedef Galois::WorkList::AltChunkedFIFO<CHUNK_SIZE, GNode> Inner_WL_ty;
  typedef Galois::WorkList::WLsizeWrapper<Inner_WL_ty> WL_ty;
  typedef PerThreadStorage<UserContextAccess<GNode> > PerThreadUserCtx;


  G& graph;
  F func;
  M& dagManager;
  const char* loopname;


  PerThreadUserCtx userContexts;

  Bag_ty nextWork;

public:

  InputGraphDAGexecutor (G& graph, const F& func, M& dagManager, const char* loopname)
    :
      graph (graph),
      func (func),
      dagManager (dagManager),
      loopname (loopname)
  {}

  void push (GNode node) {
    auto& nd = graph.getData (node, Galois::MethodFlag::UNPROTECTED);

    int expected = 0;
    if (nd.onWL.cas (expected, 1)) {
      nextWork.push (node);
    }
  }

  struct ApplyOperator {
    typedef int tt_does_not_need_aborts;

    InputGraphDAGexecutor& outer;

    template <typename C>
    void operator () (GNode src, C& ctx) {

      G& graph = outer.graph;

      auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
      assert (sd.onWL > 0); // is active
      sd.onWL = 0;

      outer.func (src, outer);

      auto closure = [&graph, &ctx] (GNode dst) {

        auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);

        if (int (dd.indegree) > 0) { // is a succ in active dag
          assert (dd.onWL > 0);

          int x = --dd.indegree; 
          assert (x >= 0);

          if (x == 0) {
            ctx.push (dst);
          }
        }
      };

      outer.dagManager.applyToDAGsucc (src, sd, closure);
    }
  };

  template <typename R>
  void execute (const R& range) {


    Galois::TimeAccumulator t_dag_init;
    Galois::TimeAccumulator t_dag_exec;

    t_dag_init.start ();
    dagManager.initDAG (); 
    t_dag_init.stop ();

    Galois::do_all_choice (
        range,
        [this] (GNode node) {
          push (node);
        }, 
        "push_initial",
        Galois::doall_chunk_size<CHUNK_SIZE> ());

    WL_ty sources;
    unsigned rounds = 0;

    while (true) {

      assert (sources.size () == 0);

      t_dag_init.start ();
      dagManager.reinitActiveDAG (Galois::Runtime::makeLocalRange (nextWork), sources);
      nextWork.clear_all_parallel ();
      t_dag_init.stop ();


      if (sources.size () == 0) {
        break;
      }

      ++rounds;

      t_dag_exec.start ();
      typedef Galois::WorkList::ExternalReference<WL_ty> WL;
      typename WL::value_type* it = 0;
      Galois::for_each(it, it,
          ApplyOperator {*this},
          Galois::loopname(loopname),
          Galois::wl<WL>(&sources));
      t_dag_exec.stop ();

      sources.reset_all ();

    }

    std::printf ("InputGraphDAGexecutor: performed %d rounds\n", rounds);
    std::printf ("InputGraphDAGexecutor: time taken by dag initialization: %lu\n", t_dag_init.get ());
    std::printf ("InputGraphDAGexecutor: time taken by dag execution: %lu\n", t_dag_exec.get ());
  }


  /*
  struct ApplyOperatorAsync {

    typedef int tt_does_not_need_aborts;

    InputGraphDAGexecutor& outer;

    template <typename C>
    void operator () (GNode src, C& ctx) {

      auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);

      if (sd.onWL > 0) {
        outer.func (src, dummyCtx); 
      }

      G& graph = outer.graph;

      auto closure = [&graph, &ctx] (GNode dst) {

        auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);

        int x = --dd.indegree;
        assert (x >= 0);
        if (x == 0) {
          ctx.push (dst);
        }
      };

      outer.dagManager.applyToAdj (src, closure);

    }
  };
  */

};


template <typename R, typename F, typename G, typename M>
void for_each_det_edge_flip_ar (const R& range, const F& func, G& graph, M& dagManager, const char* loopname) {

  Galois::Runtime::getSystemThreadPool ().burnPower (Galois::getActiveThreads ());

  InputGraphDAGexecutor<G,F, M> executor {graph, func, dagManager, loopname};

  executor.execute (range);

  Galois::Runtime::getSystemThreadPool ().beKind ();

}

template <typename R, typename F, typename G>
void for_each_det_edge_flip_ar (const R& range, const F& func, G& graph, const char* loopname) {

  typedef typename DAGmanagerInOut<G>::Manager M;
  M dagManager {graph};

  for_each_det_edge_flip_ar (range, func, graph, dagManager, loopname);
}

  // three strategies for termination
  // 1. func keeps returns true when computation converges. Terminate when all
  // nodes return true.
  // 2. ctx.push just counts the number of pushes. Terminate when 0 pushes
  // performed
  // 3. ctx.push marks the node active. Apply the func to active nodes only.
  // Terminate when no active nodes. "Activeness" can also be implemented as a
  // counter, which is incremented every time a node is marked active and
  // decremented upon processing
  //
  // Other features: 
  // 1. reinit the DAG on each round by a given priority function. 


template <typename G, typename F, typename M>
struct InputGraphDAGtopologyDriven {


  typedef typename G::GraphNode GNode;

  typedef Galois::PerThreadBag<GNode> Bag_ty;

  static const unsigned CHUNK_SIZE = F::CHUNK_SIZE;
  typedef Galois::WorkList::AltChunkedFIFO<CHUNK_SIZE, GNode> Inner_WL_ty;
  typedef Galois::WorkList::WLsizeWrapper<Inner_WL_ty> WL_ty;
  typedef PerThreadStorage<UserContextAccess<GNode> > PerThreadUserCtx;


  G& graph;
  F func;
  M& dagManager;
  const char* loopname;

  GAccumulator<size_t> numActiveFound; 
  GAccumulator<size_t> numPushes; 
  
public:

  InputGraphDAGtopologyDriven (G& graph, const F& func, M& dagManager, const char* loopname)
    :
      graph (graph),
      func (func),
      dagManager (dagManager),
      loopname (loopname)
  {}

  void push (GNode node) {
    numPushes += 1;
    auto& nd = graph.getData (node, Galois::MethodFlag::UNPROTECTED);
    // ++(nd.onWL);
    nd.onWL.cas (0, 1);
  }

  template <typename R>
  void execute (const R& range) {
    Bag_ty sources;



    Galois::TimeAccumulator t_dag_init;
    t_dag_init.start ();
    dagManager.initDAG ();
    dagManager.collectSources (sources);
    t_dag_init.stop ();

    Galois::do_all_choice (
        range,
        [this] (GNode node) {
          push (node);
        }, 
        "push_initial",
        Galois::doall_chunk_size<CHUNK_SIZE> ());

    Galois::TimeAccumulator t_dag_exec;

    unsigned rounds = 0;
    while (true) {

      ++rounds;

      assert (sources.size_all () != 0);

      auto f = [this] (GNode src) {
        auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
        if (sd.onWL > 0) { // is active
          sd.onWL = 0;
          // --(sd.onWL);

          this->func (src, *this);
          this->numActiveFound += 1;
        }
      };

      t_dag_exec.start ();
      dagManager.runDAGcomputation (f, sources, loopname);
      t_dag_exec.stop ();

      bool term = (numPushes.reduceRO () == 0);


      if (term) { break; }

      t_dag_init.start ();

      // reset
      dagManager.reinitDAG ();
      numActiveFound.reset ();
      numPushes.reset ();

      t_dag_init.stop ();

    }

    std::printf ("InputGraphDAGtopologyDriven: performed %d rounds\n", rounds);
    std::printf ("InputGraphDAGtopologyDriven: time taken by dag initialization: %lu\n", t_dag_init.get ());
    std::printf ("InputGraphDAGtopologyDriven: time taken by dag execution: %lu\n", t_dag_exec.get ());
  }


};



template <typename R, typename F, typename G, typename M>
void for_each_det_edge_flip_topo (const R& range, const F& func, G& graph, M& dagManager, const char* loopname) {

  Galois::Runtime::getSystemThreadPool ().burnPower (Galois::getActiveThreads ());

  InputGraphDAGtopologyDriven<G,F, M> executor {graph, func, dagManager, loopname};

  executor.execute (range);

  Galois::Runtime::getSystemThreadPool ().beKind ();

}

template <typename R, typename F, typename G>
void for_each_det_edge_flip_topo (const R& range, const F& func, G& graph, const char* loopname) {

  typedef typename DAGmanagerInOut<G>::Manager M;
  M dagManager {graph};

  for_each_det_edge_flip_topo (range, func, graph, dagManager, loopname);
}

template <InputDAG_ExecTy EXEC>
struct ForEachDet_InputDAG {
};


template <> 
struct ForEachDet_InputDAG<InputDAG_ExecTy::CHROMATIC> {

  template <typename R, typename F, typename G>
  static void run (const R& range, const F& func, G& graph, const char* loopname) {
    for_each_det_chromatic (range, func, graph, loopname);
  }
};

template <> 
struct ForEachDet_InputDAG<InputDAG_ExecTy::EDGE_FLIP> {

  template <typename R, typename F, typename G>
  static void run (const R& range, const F& func, G& graph, const char* loopname) {
    for_each_det_edge_flip_ar (range, func, graph, loopname);
  }
};

template <> 
struct ForEachDet_InputDAG<InputDAG_ExecTy::TOPO> {

  template <typename R, typename F, typename G>
  static void run (const R& range, const F& func, G& graph, const char* loopname) {
    for_each_det_edge_flip_topo (range, func, graph, loopname);
  }
};


} // end namespace Runtime
} // end namespace Galois

#endif // GALOIS_RUNTIME_DET_CHROMATIC_H
