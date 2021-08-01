#ifndef KATANA_LIBSUPPORT_KATANA_RDGVERSION_H_
#define KATANA_LIBSUPPORT_KATANA_RDGVERSION_H_

#include <cstring>
#include <iostream>
#include <iterator>

#include <fmt/format.h>

/*#include "katana/Result.h"*/
/*#include "katana/URI.h"*/

const uint64_t kRDGBranchIDLength = (12);

struct KATANA_EXPORT RDGVersion {
  // A version consists of mulitple BranchPoints of width, each in the form of num:id
  // The last one has an empty branch "".
  std::vector<uint64_t> numbers_;
  std::vector<std::string> branches_;
  uint64_t width_{0};

  // TODO(wkyu): to clean up these operators
#if 1
  RDGVersion(const RDGVersion& in) = default;
  RDGVersion& operator=(const RDGVersion& in) = default;
#else
  RDGVersion(const RDGVersion& in)
      : numbers_(in.numbers_), branches_(in.branches_), width_(in.width_) {}

  RDGVersion& operator=(const RDGVersion& in) {
    RDGVersion tmp = in;
    numbers_ = std::move(tmp.numbers_);
    branches_ = std::move(tmp.branches_), width_ = tmp.width_;
    return *this;
  }
#endif

  bool operator==(const RDGVersion& in) {
    return (numbers_ == in.numbers_ && branches_ == in.branches_);
  }

  bool operator>(const RDGVersion& in) { return numbers_ > in.numbers_; }

  bool operator<(const RDGVersion& in) { return numbers_ < in.numbers_; }

  RDGVersion(
      const std::vector<uint64_t>& vers, const std::vector<std::string>& ids)
      : numbers_(vers), branches_(ids) {
    width_ = vers.size() - 1;
  }

  explicit RDGVersion(uint64_t num = 0) {
    numbers_.emplace_back(num);
    branches_.emplace_back("");
    width_ = 0;
  }

  std::string GetBranchPath() const {
    std::string vec = "";
    if (numbers_.back() > 0)
      return vec;

    // return a subdir formed by branches without a trailing SepChar
    for (uint64_t i = 0; i < width_; i++) {
      vec += fmt::format("{}", branches_[i]);
      if ((i + 1) < width_)
        vec += "/";
    }
    return vec;
  }

  std::string ToVectorString() const {
    std::string vec = "";
    for (uint64_t i = 0; i < width_; i++) {
      vec += fmt::format("{}_{},", numbers_[i], branches_[i]);
    }
    // Last one has only the ver number.
    return fmt::format("{}{}", vec, numbers_[width_]);
  }

  uint64_t LeafVersionNumber() { return numbers_.back(); }

  void SetNextVersion() { numbers_[width_]++; }

  void SetBranchPoint(const std::string& name) {
    branches_[width_] = name;
    // 1 to begin a branch
    numbers_.emplace_back(1);
    branches_.emplace_back("");
    width_++;
  }

  std::vector<uint64_t>& GetVersionNumbers() { return numbers_; }

  std::vector<std::string>& GetBranchIDs() { return branches_; }
};
#endif
