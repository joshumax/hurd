
struct pipe_user
{
  struct port_info pi;
  struct pipe_end *pe;
};

struct pipe_end
{
  int refcount;
  enum { PIPE_NONE, PIPE_READ, PIPE_WRITE } type;
  struct pipe_end *peer;
  struct pipe *pipe;
};

struct pipe
{
  char *start, *end;
  size_t chars_alloced;
  struct stat *st;
  struct condition pending_reads;
  struct condition pending_selects;
  char data[0];
};
