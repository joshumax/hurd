#ifndef _HACK_INTERRUPT_H_
#define _HACK_INTERRUPT_H_

#define NET_BH 1

extern inline void 
mark_bottom_half (int foo)
{
  assert (foo == NET_BH);
  
  incoming_net_packet ();
}


#endif
