/* ---------------------------------------------------------------- */

/* A port bucket to handle SOCK_USERs and ADDRs.  */
struct port_bucket *sock_port_bucket;

/* True if there are threads servicing sock requests.  */
static int sock_server_active = 0;
static spin_lock_t sock_server_active_lock = SPIN_LOCK_INITIALIZER;

/* A demuxer for socket operations.  */
static int
sock_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  extern int socket_server (mach_msg_header_t *inp, mach_msg_header_t *outp);
  extern int io_server (mach_msg_header_t *inp, mach_msg_header_t *outp);
  extern int interrupt_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int notify_server (mach_msg_header_t *, mach_msg_header_t *);
  return
    socket_server (inp, outp)
      || io_server (inp, outp)
      || interrupt_server (inp, outp)
      || notify_server (inp, outp);
}

/* Handle socket requests while there are sockets around.  */
static void
handle_sock_requests ()
{
  while (ports_count_bucket (sock_port_bucket) > 0)
    {
      ports_enable_bucket (sock_port_bucket);
      ports_manage_port_operations_multithread (sock_port_bucket, sock_demuxer,
						30*1000, 2*60*1000,
						1, MACH_PORT_NULL);
    }

  /* The last service thread is about to exist; make this known.  */
  spin_lock (&sock_server_active_lock);
  sock_server_active = 0;
  spin_unlock (&sock_server_active_lock);

  /* Let the whole joke start once again.  */
  ports_enable_bucket (sock_port_bucket);
}

/* Makes sure there are some request threads for sock operations, and starts
   a server if necessary.  This routine should be called *after* creating the
   port(s) which need server, as the server routine only operates while there
   are any ports.  */
void
ensure_sock_server ()
{
  spin_lock (&sock_server_active_lock);
  if (sock_server_active)
    spin_unlock (&sock_server_active_lock);
  else
    {
      sock_server_active = 1;
      spin_unlock (&sock_server_active_lock);
      cthread_detach (cthread_fork ((cthread_fn_t)handle_sock_requests,
				    (any_t)0));
    }
}

/* ---------------------------------------------------------------- */
/* Notify stubs.  */

error_t
do_mach_notify_no_senders (mach_port_t port, mach_port_mscount_t count)
{
  void *pi = ports_lookup_port (sock_port_bucket, port, 0);
debug (pi, "count: %ul, refs: %d",
       count, (pi ? ((struct port_info *)pi->refcnt) : 0));
  if (!pi)
    return EOPNOTSUPP;
  ports_no_senders (pi, count);
  ports_port_deref (pi);
  return 0;
}

error_t
do_mach_notify_port_deleted (mach_port_t notify, mach_port_t name)
{
  return 0;
}

error_t
do_mach_notify_msg_accepted (mach_port_t notify, mach_port_t name)
{
  return 0;
}

error_t
do_mach_notify_port_destroyed (mach_port_t notify, mach_port_t name)
{
  return 0;
}

error_t
do_mach_notify_send_once (mach_port_t notify)
{
  return 0;
}

error_t
do_mach_notify_dead_name (mach_port_t notify, mach_port_t deadport)
{
  return 0;
}
