
/* Returns the pipe that SOCKET is reading from, locked and with an
   additional reference, or NULL if it has none.  SOCKET mustn't be locked.  */
struct pipe *
socket_aquire_read_pipe (struct socket *socket)
{
  struct pipe *pipe;

  mutex_lock (&socket->lock);
  pipe = user->socket->read_pipe;
  if (pipe != NULL)
    pipe_aquire (pipe);		/* Do this before unlocking the socket!  */
  mutex_unlock (&socket->lock);

  return pipe;
}

/* Returns the pipe that SOCKET is writing from, locked and with an
   additional reference, or NULL if it has none.  SOCKET mustn't be locked.  */
struct pipe *
socket_aquire_write_pipe (struct socket *socket)
{
  struct pipe *pipe;

  mutex_lock (&socket->lock);
  pipe = user->socket->write_pipe;
  if (pipe != NULL)
    pipe_aquire (pipe);		/* Do this before unlocking the socket!  */
  mutex_unlock (&socket->lock);

  return pipe;
}
