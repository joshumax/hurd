/*
 * lists.h:  Simple list macros for Linux
 */

#define DLNODE(ptype)			       	       	       	\
	struct {			 			\
		ptype * dl_prev;	 			\
		ptype * dl_next;	 			\
	}

#define DNODE_SINGLE(node) {(node),(node)}
#define DNODE_NULL {0,0}

#define DLIST_INIT(listnam)	                                \
	(listnam).dl_prev = &(listnam);				\
	(listnam).dl_next = &(listnam);

#define DLIST_NEXT(listnam)	listnam.dl_next
#define DLIST_PREV(listnam)	listnam.dl_prev

#define DLIST_INSERT_AFTER(node, new, listnam)	do {		\
	(new)->listnam.dl_prev = (node);			\
	(new)->listnam.dl_next = (node)->listnam.dl_next;	\
	(node)->listnam.dl_next->listnam.dl_prev = (new);	\
	(node)->listnam.dl_next = (new);			\
	} while (0)

#define DLIST_INSERT_BEFORE(node, new, listnam)	do {		\
	(new)->listnam.dl_next = (node);			\
	(new)->listnam.dl_prev = (node)->listnam.dl_prev;	\
	(node)->listnam.dl_prev->listnam.dl_next = (new);	\
	(node)->listnam.dl_prev = (new);			\
	} while (0)

#define DLIST_DELETE(node, listnam)	do {		\
	node->listnam.dl_prev->listnam.dl_next =		\
		node->listnam.dl_next;				\
	node->listnam.dl_next->listnam.dl_prev =		\
		node->listnam.dl_prev;				\
	} while (0)

/*
 * queue-style operations, which have a head and tail
 */

#define QUEUE_INIT(head, listnam, ptype)				\
	(head)->listnam.dl_prev = (head)->listnam.dl_next = (ptype)(head);

#define QUEUE_FIRST(head, listnam) (head)->DLIST_NEXT(listnam)
#define QUEUE_LAST(head, listnam) (head)->DLIST_PREV(listnam)
#define QUEUE_EMPTY(head, listnam) \
	((QUEUE_FIRST(head, listnam) == QUEUE_LAST(head, listnam)) && \
	 ((u_long)QUEUE_FIRST(head, listnam) == (u_long)head)) 

#define QUEUE_ENTER(head, new, listnam, ptype) do {		\
	(new)->listnam.dl_prev = (ptype)(head);			\
	(new)->listnam.dl_next = (head)->listnam.dl_next;	\
	(head)->listnam.dl_next->listnam.dl_prev = (new);	\
	(head)->listnam.dl_next = (new);			\
	} while (0)

#define QUEUE_REMOVE(head, node, listnam) DLIST_DELETE(node, listnam)
