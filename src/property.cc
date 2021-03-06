// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/property.h"

#include "src/handles-inl.h"
#include "src/ostreams.h"

namespace v8 {
namespace internal {

void LookupResult::Iterate(ObjectVisitor* visitor) {
  LookupResult* current = this;  // Could be NULL.
  while (current != NULL) {
    visitor->VisitPointer(bit_cast<Object**>(&current->holder_));
    visitor->VisitPointer(bit_cast<Object**>(&current->transition_));
    current = current->next_;
  }
}


std::ostream& operator<<(std::ostream& os, const LookupResult& r) {
  if (!r.IsFound()) return os << "Not Found\n";

  os << "LookupResult:\n";
  if (r.IsTransition()) {
    os << " -transition target:\n" << Brief(r.GetTransitionTarget()) << "\n";
  }
  return os;
}


std::ostream& operator<<(std::ostream& os,
                         const PropertyAttributes& attributes) {
  os << "[";
  os << (((attributes & READ_ONLY) == 0) ? "W" : "_");    // writable
  os << (((attributes & DONT_ENUM) == 0) ? "E" : "_");    // enumerable
  os << (((attributes & DONT_DELETE) == 0) ? "C" : "_");  // configurable
  os << "]";
  return os;
}


struct FastPropertyDetails {
  explicit FastPropertyDetails(const PropertyDetails& v) : details(v) {}
  const PropertyDetails details;
};


// Outputs PropertyDetails as a dictionary details.
std::ostream& operator<<(std::ostream& os, const PropertyDetails& details) {
  os << "(";
  switch (details.type()) {
    case FIELD:
      os << "normal: ";
      break;
    case CALLBACKS:
      os << "callbacks: ";
      break;
    case CONSTANT:
      UNREACHABLE();
      break;
  }
  return os << "dictionary_index: " << details.dictionary_index()
            << ", attrs: " << details.attributes() << ")";
}


// Outputs PropertyDetails as a descriptor array details.
std::ostream& operator<<(std::ostream& os,
                         const FastPropertyDetails& details_fast) {
  const PropertyDetails& details = details_fast.details;
  os << "(";
  switch (details.type()) {
    case CONSTANT:
      os << "constant: p: " << details.pointer();
      break;
    case FIELD:
      os << "field: " << details.representation().Mnemonic()
         << ", field_index: " << details.field_index()
         << ", p: " << details.pointer();
      break;
    case CALLBACKS:
      os << "callbacks: p: " << details.pointer();
      break;
  }
  return os << ", attrs: " << details.attributes() << ")";
}


#ifdef OBJECT_PRINT
void PropertyDetails::Print(bool dictionary_mode) {
  OFStream os(stdout);
  if (dictionary_mode) {
    os << *this;
  } else {
    os << FastPropertyDetails(*this);
  }
  os << "\n" << std::flush;
}
#endif


std::ostream& operator<<(std::ostream& os, const Descriptor& d) {
  return os << "Descriptor " << Brief(*d.GetKey()) << " @ "
            << Brief(*d.GetValue()) << " "
            << FastPropertyDetails(d.GetDetails());
}

} }  // namespace v8::internal
