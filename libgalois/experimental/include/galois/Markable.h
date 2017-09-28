/** Markable object -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galoisis a framework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 2.1 of the
 * License.
 *
 * Galois is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Galois.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @section Copyright
 *
 * Copyright (C) 2015, The University of Texas at Austin. All rights
 * reserved.
 *
 * @section e.g. Mark for removal, 
 *
 * Billiards Simulation Finding Partial Order
 *
 * @author <ahassaan@ices.utexas.edu>
 */

#ifndef GALOIS_UTIL_MARKED_H
#define GALOIS_UTIL_MARKED_H

#include <climits>

#include <boost/iterator/counting_iterator.hpp>

#include "galois/Galois.h"

#include "galois/runtime/DoAllCoupled.h"

namespace galois {

template <typename T>
struct Markable {
  private:
    static const unsigned MAX_VAL = UINT_MAX;

    T m_val;
    unsigned m_ver;

  public:

    explicit Markable (T val)
      : m_val (val), m_ver (MAX_VAL) 
    {}

    void mark (unsigned v) {
      assert (v < MAX_VAL);
      m_ver = v;
    }

    bool marked () const { return (m_ver < MAX_VAL); }

    unsigned version () const { return m_ver; }

    T& get () { return m_val; }

    const T& get () const { return m_val; }

    operator T& () { return get (); }

    operator const T& () const { return get (); }

};

template <typename T>
struct IsNotMarked {
  bool operator () (const Markable<T>& x) const {
    return !x.marked ();
  }
};


template <typename WL>
struct RemoveMarked {


  WL& wl;

  RemoveMarked (WL& _wl)
    : wl (_wl) {}

  void operator () (unsigned r) {
    assert (r < wl.numRows ());

    using T = typename WL::value_type;

    typename WL::local_iterator new_end =
      std::partition (wl[r].begin (), wl[r].end (), IsNotMarked<T> ());

    wl[r].erase (new_end, wl[r].end ());

  }

};

template <typename WL>
void removeMarked (WL& wl) {

  galois::do_all (
      galois::iterate(0, wl.numRows()),
      RemoveMarked<WL> (wl),
      "remove_marked");
      
}

template <typename WL>
struct RemoveMarkedStable: public RemoveMarked<WL> {

  using Super_ty = RemoveMarked<WL>;

  RemoveMarkedStable (WL& _wl): Super_ty (_wl) {}

  void operator () (unsigned r) {
    assert (r < Super_ty::wl.numRows ());

    using T = typename WL::value_type;
    
    typename WL::local_iterator new_end =
      std::stable_partition (Super_ty::wl[r].begin (), Super_ty::wl[r].end (), IsNotMarked<T> ());

    Super_ty::wl[r].erase (new_end, Super_ty::wl[r].end ());
    
  }
};

template <typename WL>
void removeMarkedStable (WL& wl) {

  galois::do_all (
      galois::iterate(0, wl.numRows()),
      RemoveMarkedStable<WL> (wl),
      "remove_marked_stable");
      
}


} // end namespace galois



#endif // GALOIS_UTIL_MARKED_H

