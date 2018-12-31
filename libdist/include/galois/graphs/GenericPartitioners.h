#ifndef _GALOIS_DIST_GENERICPARTS_H
#define _GALOIS_DIST_GENERICPARTS_H

#include "DistributedGraph.h"
#include <utility>
#include <cmath>

class NoCommunication {
  std::vector<std::pair<uint64_t, uint64_t>> _gid2host;
  uint32_t _numHosts;

 public:
  NoCommunication(uint32_t, uint32_t numHosts) {
    _numHosts = numHosts;
  }

  void saveGIDToHost(std::vector<std::pair<uint64_t, uint64_t>>& gid2host) {
    _gid2host = gid2host;
  }

  uint32_t getMaster(uint32_t gid) const {
    for (auto h = 0U; h < _numHosts; ++h) {
      uint64_t start, end;
      std::tie(start, end) = _gid2host[h];
      if (gid >= start && gid < end) {
        return h;
      }
    }
    assert(false);
    return _numHosts;
  }

  uint32_t getEdgeOwner(uint32_t src, uint32_t, uint64_t) const {
    return getMaster(src);
  }

  bool isVertexCut() const {
    return false;
  }

  bool isCartCut() {
    return false;
  }

  bool isNotCommunicationPartner(unsigned, unsigned, WriteLocation,
                                 ReadLocation, bool) {
    return false;
  }

  void serializePartition(boost::archive::binary_oarchive&) {
    return;
  }

  void deserializePartition(boost::archive::binary_iarchive&) {
    return;
  }

  bool noCommunication() {
    return true;
  }
};

class GenericCVC {
  std::vector<std::pair<uint64_t, uint64_t>> _gid2host;
  uint32_t _hostID;
  uint32_t _numHosts;
  unsigned numRowHosts;
  unsigned numColumnHosts;
  unsigned _h_offset;

  void factorizeHosts() {
    numColumnHosts  = sqrt(_numHosts);

    while ((_numHosts % numColumnHosts) != 0)
      numColumnHosts--;

    numRowHosts = _numHosts / numColumnHosts;
    assert(numRowHosts >= numColumnHosts);

    //if (moreColumnHosts) {
    //  std::swap(numRowHosts, numColumnHosts);
    //}

    if (_hostID == 0) {
      galois::gPrint("Cartesian grid: ", numRowHosts, " x ", numColumnHosts,
                     "\n");
    }
  }

  //! Returns the grid row ID of this host
  unsigned gridRowID() const { return (_hostID / numColumnHosts); }
  //! Returns the grid row ID of the specified host
  unsigned gridRowID(unsigned id) const { return (id / numColumnHosts); }
  //! Returns the grid column ID of this host
  unsigned gridColumnID() const {
    return (_hostID % numColumnHosts);
  }
  //! Returns the grid column ID of the specified host
  unsigned gridColumnID(unsigned id) const {
    return (id % numColumnHosts);
  }

  //! Find the column of a particular node
  unsigned getColumnOfNode(uint64_t gid) const {
    return gridColumnID(getMaster(gid));
  }

 public:
  GenericCVC(uint32_t hostID, uint32_t numHosts) {
    _hostID = hostID;
    _numHosts = numHosts;
    factorizeHosts();
    _h_offset = gridRowID() * numColumnHosts;
  }

  void saveGIDToHost(std::vector<std::pair<uint64_t, uint64_t>>& gid2host) {
    _gid2host = gid2host;
  }

  uint32_t getMaster(uint32_t gid) const {
    for (auto h = 0U; h < _numHosts; ++h) {
      uint64_t start, end;
      std::tie(start, end) = _gid2host[h];
      if (gid >= start && gid < end) {
        return h;
      }
    }
    assert(false);
    return _numHosts;
  }

  uint32_t getEdgeOwner(uint32_t src, uint32_t dst, uint64_t numEdges) const {
    int i         = getColumnOfNode(dst);
    return _h_offset + i;
  }

  bool isVertexCut() const {
    if ((numRowHosts == 1) || (numColumnHosts == 1)) return false;
    return true;
  }

  constexpr static bool isCartCut() {
    return true;
  }

  bool isNotCommunicationPartner(unsigned host, unsigned syncType,
                                 WriteLocation writeLocation,
                                 ReadLocation readLocation,
                                 bool transposed) {
    if (transposed) {
      if (syncType == 0) {
        switch (writeLocation) {
        case writeSource:
          return (gridColumnID() != gridColumnID(host));
        case writeDestination:
          return (gridRowID() != gridRowID(host));
        case writeAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      } else { // syncBroadcast
        switch (readLocation) {
        case readSource:
          return (gridColumnID() != gridColumnID(host));
        case readDestination:
          return (gridRowID() != gridRowID(host));
        case readAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      }
    } else {
      if (syncType == 0) {
        switch (writeLocation) {
        case writeSource:
          return (gridRowID() != gridRowID(host));
        case writeDestination:
          return (gridColumnID() != gridColumnID(host));
        case writeAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      } else { // syncBroadcast, 1
        switch (readLocation) {
        case readSource:
          return (gridRowID() != gridRowID(host));
        case readDestination:
          return (gridColumnID() != gridColumnID(host));
        case readAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      }
      return false;
    }
    return false;
  }

  void serializePartition(boost::archive::binary_oarchive& ar) {
    ar << numRowHosts;
    ar << numColumnHosts;
  }

  void deserializePartition(boost::archive::binary_iarchive& ar) {
    ar >> numRowHosts;
    ar >> numColumnHosts;
  }

  bool noCommunication() {
    return false;
  }
};

// same as above, except columns are flipped (changes behavior of vertex cut
// call as well)
class GenericCVCColumnFlip {
  std::vector<std::pair<uint64_t, uint64_t>> _gid2host;
  uint32_t _hostID;
  uint32_t _numHosts;
  unsigned numRowHosts;
  unsigned numColumnHosts;
  unsigned _h_offset;

  void factorizeHosts() {
    numColumnHosts  = sqrt(_numHosts);

    while ((_numHosts % numColumnHosts) != 0)
      numColumnHosts--;

    numRowHosts = _numHosts / numColumnHosts;
    assert(numRowHosts >= numColumnHosts);

    // column flip
    std::swap(numRowHosts, numColumnHosts);

    if (_hostID == 0) {
      galois::gPrint("Cartesian grid: ", numRowHosts, " x ", numColumnHosts,
                     "\n");
    }
  }

  //! Returns the grid row ID of this host
  unsigned gridRowID() const { return (_hostID / numColumnHosts); }
  //! Returns the grid row ID of the specified host
  unsigned gridRowID(unsigned id) const { return (id / numColumnHosts); }
  //! Returns the grid column ID of this host
  unsigned gridColumnID() const {
    return (_hostID % numColumnHosts);
  }
  //! Returns the grid column ID of the specified host
  unsigned gridColumnID(unsigned id) const {
    return (id % numColumnHosts);
  }

  //! Find the column of a particular node
  unsigned getColumnOfNode(uint64_t gid) const {
    return gridColumnID(getMaster(gid));
  }

 public:
  GenericCVCColumnFlip(uint32_t hostID, uint32_t numHosts) {
    _hostID = hostID;
    _numHosts = numHosts;
    factorizeHosts();
    _h_offset = gridRowID() * numColumnHosts;
  }

  void saveGIDToHost(std::vector<std::pair<uint64_t, uint64_t>>& gid2host) {
    _gid2host = gid2host;
  }

  uint32_t getMaster(uint32_t gid) const {
    for (auto h = 0U; h < _numHosts; ++h) {
      uint64_t start, end;
      std::tie(start, end) = _gid2host[h];
      if (gid >= start && gid < end) {
        return h;
      }
    }
    assert(false);
    return _numHosts;
  }

  uint32_t getEdgeOwner(uint32_t src, uint32_t dst, uint64_t numEdges) const {
    int i         = getColumnOfNode(dst);
    return _h_offset + i;
  }

  bool isVertexCut() const {
    if ((numRowHosts == 1) && (numColumnHosts == 1)) return false;
    return true;
  }

  constexpr static bool isCartCut() {
    return true;
  }

  bool isNotCommunicationPartner(unsigned host, unsigned syncType,
                                 WriteLocation writeLocation,
                                 ReadLocation readLocation,
                                 bool transposed) {
    if (transposed) {
      if (syncType == 0) {
        switch (writeLocation) {
        case writeSource:
          return (gridColumnID() != gridColumnID(host));
        case writeDestination:
          return (gridRowID() != gridRowID(host));
        case writeAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      } else { // syncBroadcast
        switch (readLocation) {
        case readSource:
          return (gridColumnID() != gridColumnID(host));
        case readDestination:
          return (gridRowID() != gridRowID(host));
        case readAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      }
    } else {
      if (syncType == 0) {
        switch (writeLocation) {
        case writeSource:
          return (gridRowID() != gridRowID(host));
        case writeDestination:
          return (gridColumnID() != gridColumnID(host));
        case writeAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      } else { // syncBroadcast, 1
        switch (readLocation) {
        case readSource:
          return (gridRowID() != gridRowID(host));
        case readDestination:
          return (gridColumnID() != gridColumnID(host));
        case readAny:
          assert((gridRowID() == gridRowID(host)) ||
                 (gridColumnID() == gridColumnID(host)));
          return ((gridRowID() != gridRowID(host)) &&
                  (gridColumnID() != gridColumnID(host))); // false
        default:
          assert(false);
        }
      }
      return false;
    }
    return false;
  }

  void serializePartition(boost::archive::binary_oarchive& ar) {
    ar << numRowHosts;
    ar << numColumnHosts;
  }

  void deserializePartition(boost::archive::binary_iarchive& ar) {
    ar >> numRowHosts;
    ar >> numColumnHosts;
  }

  bool noCommunication() {
    return false;
  }
};

class GenericHVC {
  std::vector<std::pair<uint64_t, uint64_t>> _gid2host;
  uint32_t _hostID;
  uint32_t _numHosts;
  uint32_t _vCutThreshold;
 public:
  GenericHVC(uint32_t hostID, uint32_t numHosts) {
    _hostID = hostID;
    _numHosts = numHosts;
    _vCutThreshold = 1000; // can be changed, but default seems to be 1000
  }

  void saveGIDToHost(std::vector<std::pair<uint64_t, uint64_t>>& gid2host) {
    _gid2host = gid2host;
  }

  uint32_t getMaster(uint32_t gid) const {
    for (auto h = 0U; h < _numHosts; ++h) {
      uint64_t start, end;
      std::tie(start, end) = _gid2host[h];
      if (gid >= start && gid < end) {
        return h;
      }
    }
    assert(false);
    return _numHosts;
  }

  uint32_t getEdgeOwner(uint32_t src, uint32_t dst, uint64_t numEdges) const {
    if (numEdges > _vCutThreshold) {
      return getMaster(dst);
    } else {
      return getMaster(src);
    }
  }

  // TODO I should be able to make this runtime detectable
  bool isVertexCut() const {
    return true;
  }

  constexpr static bool isCartCut() {
    return false;
  }

  // not used by this
  bool isNotCommunicationPartner(unsigned host, unsigned syncType,
                                 WriteLocation writeLocation,
                                 ReadLocation readLocation,
                                 bool transposed) {
    return false;
  }

  void serializePartition(boost::archive::binary_oarchive& ar) {
    return;
  }

  void deserializePartition(boost::archive::binary_iarchive& ar) {
    return;
  }

  bool noCommunication() {
    return false;
  }
};

class GingerP{
  std::vector<std::pair<uint64_t, uint64_t>> _gid2host;
  uint32_t _hostID;
  uint32_t _numHosts;
  uint32_t _vCutThreshold;
  uint64_t _numNodes;
  uint64_t _numEdges;
  // ginger scoring constants
  double _gamma;
  double _alpha;
  // ginger node/edge ratio
  double _neRatio;

      // gamma = 3/2
      // alpha = sqrt # hosts * edges divided by number of nodes power 3/2
      // x ^ (gamma - 1)
      // balance: # nodes on partition + # edges on partition * 3/2 all
      // divided by 2

 public:
  GingerP(uint32_t hostID, uint32_t numHosts, uint64_t numNodes,
         uint64_t numEdges) {
    _hostID = hostID;
    _numHosts = numHosts;
    _vCutThreshold = 10000;
    _numNodes = numNodes;
    _numEdges = numEdges;
    _gamma = 1.5;
    _alpha = numEdges * pow(numHosts, _gamma - 1.0) / pow(numNodes, _gamma);
    galois::gDebug("Alpha is ", _alpha);
    _neRatio = (double)numNodes / (double)numEdges;
  }

  void saveGIDToHost(std::vector<std::pair<uint64_t, uint64_t>>& gid2host) {
    _gid2host = gid2host;
  }

  // TODO
  uint32_t getMaster(uint32_t gid) const {
    for (auto h = 0U; h < _numHosts; ++h) {
      uint64_t start, end;
      std::tie(start, end) = _gid2host[h];
      if (gid >= start && gid < end) {
        return h;
      }
    }
    return _numHosts;
  }

  uint32_t getMaster(uint32_t gid,
                     std::map<uint64_t, uint32_t> mapping) const {
    for (auto h = 0U; h < _numHosts; ++h) {
      uint64_t start, end;
      std::tie(start, end) = _gid2host[h];
      if (gid >= start && gid < end) {
        return h;
      }
    }

    assert(false);
    return _numHosts;
  }

  /**
   * Returns Ginger's composite balance parameter for a given host
   */
  double getCompositeBalanceParam(unsigned host,
          const std::vector<uint64_t>& nodeLoads,
          const std::vector<galois::CopyableAtomic<uint64_t>>& nodeAccum,
          const std::vector<uint64_t>& edgeLoads,
          const std::vector<galois::CopyableAtomic<uint64_t>>& edgeAccum) {
    // get node/edge loads
    uint64_t hostNodeLoad = nodeLoads[host] + nodeAccum[host].load();
    uint64_t hostEdgeLoad = edgeLoads[host] + edgeAccum[host].load();

    return (hostNodeLoad + (_neRatio * hostEdgeLoad)) / 2;
  }

  /**
   * Use FENNEL balance equation to get a score value for partition
   * scoring
   */
  double getFennelBalanceScore(double param) {
    return _alpha * _gamma * pow(param, _gamma - 1);
  }

  template<typename EdgeTy>
  uint32_t determineMaster(uint32_t src,
      galois::graphs::BufferedGraph<EdgeTy>& bufGraph,
      const std::vector<uint32_t>& localNodeToMaster,
      std::map<uint64_t, uint32_t>& gid2offsets,
      const std::vector<uint64_t>& nodeLoads,
      std::vector<galois::CopyableAtomic<uint64_t>>& nodeAccum,
      const std::vector<uint64_t>& edgeLoads,
      std::vector<galois::CopyableAtomic<uint64_t>>& edgeAccum) {

    auto ii = bufGraph.edgeBegin(src);
    auto ee = bufGraph.edgeEnd(src);
    // number of edges
    uint64_t ne = std::distance(ii, ee);

    // high in-degree nodes masters stay the same
    if (ne > _vCutThreshold) {
      return _hostID;
    } else {
    // low in degree masters move based on augmented FENNEL scoring metric
      // initialize array to hold scores
      galois::PODResizeableArray<double> scores;
      scores.resize(_numHosts);
      for (unsigned i = 0; i < _numHosts; i++) {
        scores[i] = 0.0;
      }

      for (; ii < ee; ++ii) {
        uint64_t dst = bufGraph.edgeDestination(*ii);
        size_t offsetIntoMap = (unsigned)-1;

        if (getHostReader(dst) != _hostID) {
          // offset can be found using map
          assert(gid2offsets.find(dst) != gid2offsets.end());
          offsetIntoMap = gid2offsets[dst];
        } else {
          // determine offset
          offsetIntoMap = dst - bufGraph.getNodeOffset();
        }

        assert(offsetIntoMap != (unsigned)-1);
        assert(offsetIntoMap >= 0);
        assert(offsetIntoMap < localNodeToMaster.size());

        unsigned currentAssignment = localNodeToMaster[offsetIntoMap];

        if (currentAssignment != (unsigned)-1) {
          scores[currentAssignment] += 1.0;
        } else {
          galois::gDebug("[", _hostID, "] ", dst, " unassigned");
        }
      }

      // subtraction of the composite balance term
      for (unsigned i = 0; i < _numHosts; i++) {

      }

      // alpha * gamma * balance ^ gamma - 1
      // gamma = 3/2
      // alpha = sqrt # hosts * edges divided by number of nodes power 3/2
      // x ^ (gamma - 1)
      // balance: # nodes on partition + # edges on partition * 3/2 all
      // divided by 2

      unsigned bestHost = -1;
      double bestScore = 0;
      // find max score
      for (unsigned i = 0; i < _numHosts; i++) {
        if (scores[i] >= bestScore) {
          if (scores[i] > 0) {
            galois::gDebug("best score ", bestScore, " beaten by ", scores[i]);
          }
          bestScore = scores[i];
          bestHost = i;
        }
      }

      galois::gDebug("[", _hostID, "] ", src, " assigned to ", bestHost,
                     " with num edge ", ne);

      // update metadata; TODO make this a nicer interface
      galois::atomicAdd(nodeAccum[bestHost], (uint64_t)1);
      galois::atomicAdd(edgeAccum[bestHost], ne);

      return bestHost;
    }
  }


  // TODO
  uint32_t getEdgeOwner(uint32_t src, uint32_t dst, uint64_t numEdges) const {
    // if high indegree, then stay on destination (src), else get moved to source (dst)
    // note "dst" here is actually the source since we're reading transpose
    if (numEdges > _vCutThreshold) {
      return getMaster(dst);
    } else {
      return getMaster(src);
    }
  }

  // TODO I should be able to make this runtime detectable
  bool isVertexCut() const {
    return true;
  }

  constexpr static bool isCartCut() {
    return false;
  }

  // not used by this
  bool isNotCommunicationPartner(unsigned host, unsigned syncType,
                                 WriteLocation writeLocation,
                                 ReadLocation readLocation,
                                 bool transposed) {
    return false;
  }

  void serializePartition(boost::archive::binary_oarchive& ar) {
    return;
  }

  void deserializePartition(boost::archive::binary_iarchive& ar) {
    return;
  }

  bool noCommunication() {
    return false;
  }

  /**
   * get reader of a particular node
   */
  unsigned getHostReader(uint64_t gid) const {
    for (auto i = 0U; i < _numHosts; ++i) {
      uint64_t start, end;
      std::tie(start, end) = _gid2host[i];
      if (gid >= start && gid < end) {
        return i;
      }
    }
    return -1;
  }

};

#endif
