#ifndef BOGOLYANDRAS_STR_VEC_H
#define BOGOLYANDRAS_STR_VEC_H

#include "datastructs/vector.h"
#include "pstring.h"

VECTOR_DEFINE(PChar *, StrVec)

/** Free the owned strings of a StrVec and clear it, keeping the capacity */
void StrVec_clear_owned(StrVec *vec);
/** Free a StrVec whose elements are heap strings it owns: frees the strings and the vector storage */
void StrVec_free_owned(StrVec *vec);

#endif //BOGOLYANDRAS_STR_VEC_H
