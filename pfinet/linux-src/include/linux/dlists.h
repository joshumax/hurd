#ifndef DLISTS_H
#define DLISTS_H
/*
 * include/linux/dlists.h - macros for double linked lists
 *
 * Copyright (C) 1997, Thomas Schoebel-Theuer,
 * <schoebel@informatik.uni-stuttgart.de>.
 */

/* dlists are cyclic ringlists, so the last element cannot be tested
 * for NULL. Use the following construct for traversing cyclic lists:
 * ptr = anchor;
 * if(ptr) do {
 *      ...
 *      ptr = ptr->{something}_{next,prev};
 * } while(ptr != anchor);
 * The effort here is paid off with much simpler inserts/removes.
 * Examples for usage of these macros can be found in fs/ninode.c.
 * To access the last element in constant time, simply use
 * anchor->{something}_prev.
 */

#define DEF_GENERIC_INSERT(CHANGE,PREFIX,NAME,TYPE,NEXT,PREV) \
static inline void PREFIX##NAME(TYPE ** anchor, TYPE * elem)\
{\
	TYPE * oldfirst = *anchor;\
	if(!oldfirst) {\
		elem->NEXT = elem->PREV = *anchor = elem;\
	} else {\
		elem->PREV = oldfirst->PREV;\
		elem->NEXT = oldfirst;\
		oldfirst->PREV->NEXT = elem;\
		oldfirst->PREV = elem;\
		if(CHANGE)\
			*anchor = elem;\
	}\
}

/* insert_* is always at the first position */
#define DEF_INSERT(NAME,TYPE,NEXT,PREV) \
                   DEF_GENERIC_INSERT(1,insert_,NAME,TYPE,NEXT,PREV)

/* append_* is always at the tail  */
#define DEF_APPEND(NAME,TYPE,NEXT,PREV) \
                   DEF_GENERIC_INSERT(0,append_,NAME,TYPE,NEXT,PREV)

/* use this to insert _before_ oldelem somewhere in the middle of the list.
 * the list must not be empty, and oldelem must be already a member.*/
#define DEF_INSERT_MIDDLE(NAME,TYPE) \
static inline void insert_middle_##NAME(TYPE ** anchor, TYPE * oldelem, TYPE * elem)\
{\
	int status = (oldelem == *anchor);\
	insert_##NAME(&oldelem, elem);\
	if(status)\
		*anchor = oldelem;\
}

/* remove can be done with any element in the list */
#define DEF_REMOVE(NAME,TYPE,NEXT,PREV) \
static inline void remove_##NAME(TYPE ** anchor, TYPE * elem)\
{\
	TYPE * next = elem->NEXT;\
	if(next == elem) {\
		*anchor = NULL;\
	} else {\
		TYPE * prev = elem->PREV;\
		prev->NEXT = next;\
		next->PREV = prev;\
		elem->NEXT = elem->PREV = NULL;/*leave this during debugging*/\
		if(*anchor == elem)\
			*anchor = next;\
	}\
}


/* According to ideas from David S. Miller, here is a slightly
 * more efficient plug-in compatible version using non-cyclic lists,
 * but allowing neither backward traversals nor constant time access
 * to the last element.
 * Note that although the interface is the same, the PPREV pointer must be
 * declared doubly indirect and the test for end-of-list is different. */

/* as above, this inserts always at the head */
#define DEF_LIN_INSERT(NAME,TYPE,NEXT,PPREV) \
static inline void insert_##NAME(TYPE ** anchor, TYPE * elem)\
{\
	TYPE * first;\
	if((elem->NEXT = first = *anchor))\
		first->PPREV = &elem->NEXT;\
	*anchor = elem;\
	elem->PPREV = anchor;\
}

/* as above, this works with any list element */
#define DEF_LIN_REMOVE(NAME,TYPE,NEXT,PPREV) \
static inline void remove_##NAME(TYPE ** anchor, TYPE * elem)\
{\
	TYPE * pprev;\
	if((pprev = elem->PPREV)) {\
		TYPE * next;\
		if((next = elem->NEXT))\
			next->PPREV = pprev;\
		*pprev = next;\
		elem->PPREV = elem->NEXT = NULL; /*leave this for debugging*/\
	}\
}

#endif
