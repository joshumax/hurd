/* 
   Copyright (C) 1994 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */



struct rpc_list
{
  struct rpc_list *next, **prevp;

  void *rpc;
  size_t rpc_len;
  void *reply;
  size_t reply_buflen;
  size_t reply_len;
  int reply_received;
};

static struct rpc_list *outstanding_rpcs;
static struct mutex outstanding_lock = MUTEX_INITIALIZER;
static struct condition rpc_wakeup = CONDITION_INITIALIZER;

/* Send an RPC message to target TARGET from buffer BUF of size
   LEN, waiting for a reply.  The expected reply length is
   REPLY_LEN; a buffer that big is provided as REPLY_BUF.
   Set REPLY_CONTENTS to the actual protocol-level reply, of
   size REPLY_CONTENTS_LEN.  AUTH is the authentication used
   in constructing BUF.*/
error_t
rpc_send (struct rpc_target *target, void *buf, size_t len,
	  void *reply_buf, size_t reply_len, void **reply_contents,
	  size_t *reply_contents_len, struct rpc_auth *auth)
{
  struct rpc_list record;
  struct rpcv2_msg_hdr *reply;
  struct rpcv2_msg_hdr *request;
  struct rpcv2_accepted_reply *accept;
  struct rpcv2_rejected_reply *reject;
  struct rpcv2_auth *authdata;
  struct rpcv2_verf *verf;
  error_t error;

  mutex_lock (&outstanding_lock);

  record.rpc = request = buf;
  record.len = len;
  record.reply = reply_buf;
  record.reply_len = reply_len;
  record.reply_received = 0;

  record.next = outstanding_rpcs;
  if (record.next)
    record.next->prevp = &record.next;
  outstanding_rpcs = &record;
  record.prevp = &outstanding_rpcs;
  
  rpc_transmit (target, &record);
  
  while (!record.reply_received)
    condition_wait (&rpc_wakeup, &outstanding_lock);
  
  /* Examine the reply to see whether any RPC level errors have happened. */
  
  reply = record.reply;
  
  assert (reply->xid == request->xid);
  assert (reply->mtype == RPCV2_REPLY);
  
  if (reply->body.reply.reply_state == RPCV2_MSG_ACCEPTED)
    {
      /* Ignore VERF */
      verf = &reply->body.reply.rest;
      accept = (void *)verf + sizeof (struct rpcv2_verf) + verf->nbytes;

      /* The message was accepted; return the appropriate error
	 (or success) to the caller. */
      if (accept->stat == RPCV2_SUCCESS)
	{
	  /* Ah hah!  Return to the user */
	  *reply_contents = &accept->reply_data.results;
	  *reply_contents_len = record.reply_len;
	  *reply_contents_len -= ((void *)&accept->reply_data.result 
				  - (void *) reply);
	  error = 0;
	}
      else
	error = EOPNOTSUPP;
    }
  else if (reply->body.reply.reply_state == RPCV2_MSG_DENIED)
    {
      /* For some reason we didn't win. */
      reject = &reply->body.reply.rest;
      if (reject->stat == RPCV2_RPC_MISMATCH)
	error = EOPNOTSUPP;
      else if (reject->stat == RPCV2_AUTH_ERROR)
	error = EACCES;
      else
	error = EIEIO;
    }
  else
    error = EIEIO;
  
  *record.prevp = record.next;
  if (record.next)
    record.next->prevp = record.prevp;
      
  mutex_unlock (&outstanding_lock);
  return error;
}

