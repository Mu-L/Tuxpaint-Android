#include "gifenc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

/* helper to write a little-endian 16-bit number portably */
#define write_num(fd, n) write((fd), (uint8_t []) {(n) & 0xFF, (n) >> 8}, 2)

static uint8_t vga[0x30] = {
  0x00, 0x00, 0x00,
  0xAA, 0x00, 0x00,
  0x00, 0xAA, 0x00,
  0xAA, 0x55, 0x00,
  0x00, 0x00, 0xAA,
  0xAA, 0x00, 0xAA,
  0x00, 0xAA, 0xAA,
  0xAA, 0xAA, 0xAA,
  0x55, 0x55, 0x55,
  0xFF, 0x55, 0x55,
  0x55, 0xFF, 0x55,
  0xFF, 0xFF, 0x55,
  0x55, 0x55, 0xFF,
  0xFF, 0x55, 0xFF,
  0x55, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF,
};

struct Node
{
  uint16_t key;
  struct Node *children[];
};
typedef struct Node Node;

static Node *new_node(uint16_t key, int degree)
{
  Node *node = calloc(1, sizeof(*node) + degree * sizeof(Node *));

  if (node)
    node->key = key;
  return node;
}

static Node *new_trie(int degree, int *nkeys)
{
  Node *root = new_node(0, degree);

  /* Create nodes for single pixels. */
  for (*nkeys = 0; *nkeys < degree; (*nkeys)++)
    root->children[*nkeys] = new_node(*nkeys, degree);
  *nkeys += 2;                  /* skip clear code and stop code */
  return root;
}

static void del_trie(Node *root, int degree)
{
  int i;

  if (!root)
    return;
  for (i = 0; i < degree; i++)
    del_trie(root->children[i], degree);
  free(root);
}

static void put_loop(ge_GIF * gif, uint16_t loop);

#define OR_ABORT if (res == -1) { fprintf(stderr, "Cannot write to GIF\n"); return(NULL); }
#define OR_ABORT2 if (res == -1) { fprintf(stderr, "Cannot write to GIF\n"); return; }

ge_GIF *ge_new_gif(const char *fname, uint16_t width, uint16_t height, uint8_t *palette, int depth, int loop)
{
  int i, r, g, b, v;
  ssize_t res;
  ge_GIF *gif = calloc(1, sizeof(*gif) + 2 * width * height);

  if (!gif)
    goto no_gif;
  gif->w = width;
  gif->h = height;
  gif->depth = depth > 1 ? depth : 2;
  gif->frame = (uint8_t *) & gif[1];
  gif->back = &gif->frame[width * height];
  gif->fd = creat(fname, 0666);
  if (gif->fd == -1)
    goto no_fd;
#ifdef _WIN32
  setmode(gif->fd, O_BINARY);
#endif
  res = write(gif->fd, "GIF89a", 6);
  OR_ABORT;
  res = write_num(gif->fd, width);
  OR_ABORT;
  res = write_num(gif->fd, height);
  OR_ABORT;
  res = write(gif->fd, (uint8_t[])
              {
              0xF0 | (depth - 1), 0x00, 0x00}
              , 3);
  OR_ABORT;
  if (palette)
  {
    res = write(gif->fd, palette, 3 << depth);
    OR_ABORT;
  }
  else if (depth <= 4)
  {
    res = write(gif->fd, vga, 3 << depth);
    OR_ABORT;
  }
  else
  {
    res = write(gif->fd, vga, sizeof(vga));
    OR_ABORT;
    i = 0x10;
    for (r = 0; r < 6; r++)
    {
      for (g = 0; g < 6; g++)
      {
        for (b = 0; b < 6; b++)
        {
          res = write(gif->fd, (uint8_t[])
                      {
                      r * 51, g * 51, b * 51}
                      , 3);
          OR_ABORT;
          if (++i == 1 << depth)
            goto done_gct;
        }
      }
    }
    for (i = 1; i <= 24; i++)
    {
      v = i * 0xFF / 25;
      res = write(gif->fd, (uint8_t[])
                  {
                  v, v, v}
                  , 3);
      OR_ABORT;
    }
  }
done_gct:
  if (loop >= 0 && loop <= 0xFFFF)
    put_loop(gif, (uint16_t) loop);
  return gif;
no_fd:
  free(gif);
no_gif:
  return NULL;
}

static void put_loop(ge_GIF *gif, uint16_t loop)
{
  ssize_t res;

  res = write(gif->fd, (uint8_t[])
              {
              '!', 0xFF, 0x0B}, 3);
  OR_ABORT2;
  res = write(gif->fd, "NETSCAPE2.0", 11);
  OR_ABORT2;
  res = write(gif->fd, (uint8_t[])
              {
              0x03, 0x01}, 2);
  OR_ABORT2;
  res = write_num(gif->fd, loop);
  OR_ABORT2;
  res = write(gif->fd, "\0", 1);
  OR_ABORT2;
}

/* Add packed key to buffer, updating offset and partial.
 *   gif->offset holds position to put next *bit*
 *   gif->partial holds bits to include in next byte */
static void put_key(ge_GIF *gif, uint16_t key, int key_size)
{
  int byte_offset, bit_offset, bits_to_write;
  ssize_t res;

  byte_offset = gif->offset / 8;
  bit_offset = gif->offset % 8;
  gif->partial |= ((uint32_t) key) << bit_offset;
  bits_to_write = bit_offset + key_size;
  while (bits_to_write >= 8)
  {
    gif->buffer[byte_offset++] = gif->partial & 0xFF;
    if (byte_offset == 0xFF)
    {
      res = write(gif->fd, "\xFF", 1);
      OR_ABORT2;
      res = write(gif->fd, gif->buffer, 0xFF);
      OR_ABORT2;
      byte_offset = 0;
    }
    gif->partial >>= 8;
    bits_to_write -= 8;
  }
  gif->offset = (gif->offset + key_size) % (0xFF * 8);
}

static void end_key(ge_GIF *gif)
{
  int byte_offset;
  ssize_t res;

  byte_offset = gif->offset / 8;
  if (gif->offset % 8)
    gif->buffer[byte_offset++] = gif->partial & 0xFF;
  res = write(gif->fd, (uint8_t[])
              {
              byte_offset}
              , 1);
  OR_ABORT2;
  res = write(gif->fd, gif->buffer, byte_offset);
  OR_ABORT2;
  res = write(gif->fd, "\0", 1);
  OR_ABORT2;
  gif->offset = gif->partial = 0;
}

static void put_image(ge_GIF *gif, uint16_t w, uint16_t h, uint16_t x, uint16_t y)
{
  int nkeys, key_size, i, j;
  Node *node, *child, *root;
  ssize_t res;
  int degree = 1 << gif->depth;

  res = write(gif->fd, ",", 1);
  OR_ABORT2;
  res = write_num(gif->fd, x);
  OR_ABORT2;
  res = write_num(gif->fd, y);
  OR_ABORT2;
  res = write_num(gif->fd, w);
  OR_ABORT2;
  res = write_num(gif->fd, h);
  OR_ABORT2;
  res = write(gif->fd, (uint8_t[])
              {
              0x00, gif->depth}, 2);
  OR_ABORT2;
  root = node = new_trie(degree, &nkeys);
  key_size = gif->depth + 1;
  put_key(gif, degree, key_size);       /* clear code */
  for (i = y; i < y + h; i++)
  {
    for (j = x; j < x + w; j++)
    {
      uint8_t pixel = gif->frame[i * gif->w + j] & (degree - 1);

      child = node->children[pixel];
      if (child)
      {
        node = child;
      }
      else
      {
        put_key(gif, node->key, key_size);
        if (nkeys < 0x1000)
        {
          if (nkeys == (1 << key_size))
            key_size++;
          node->children[pixel] = new_node(nkeys++, degree);
        }
        else
        {
          put_key(gif, degree, key_size);       /* clear code */
          del_trie(root, degree);
          root = node = new_trie(degree, &nkeys);
          key_size = gif->depth + 1;
        }
        node = root->children[pixel];
      }
    }
  }
  put_key(gif, node->key, key_size);
  put_key(gif, degree + 1, key_size);   /* stop code */
  end_key(gif);
  del_trie(root, degree);
}

static int get_bbox(ge_GIF *gif, uint16_t *w, uint16_t *h, uint16_t *x, uint16_t *y)
{
  int i, j, k;
  int left, right, top, bottom;

  left = gif->w;
  right = 0;
  top = gif->h;
  bottom = 0;
  k = 0;
  for (i = 0; i < gif->h; i++)
  {
    for (j = 0; j < gif->w; j++, k++)
    {
      if (gif->frame[k] != gif->back[k])
      {
        if (j < left)
          left = j;
        if (j > right)
          right = j;
        if (i < top)
          top = i;
        if (i > bottom)
          bottom = i;
      }
    }
  }
  if (left != gif->w && top != gif->h)
  {
    *x = left;
    *y = top;
    *w = right - left + 1;
    *h = bottom - top + 1;
    return 1;
  }
  else
  {
    return 0;
  }
}

/* (From the docs)
 * The `delay` parameter  specifies how long the frame will  be shown, in hundreths
 * of a second. For example, `delay` ==  100 means "show this frame for one second"
 * and `delay` == 25  means "show this frame for a quarter of  a second". Note that
 * short delays may not be supported by  some GIF viewers: it's recommended to keep
 * a minimum of `delay` == 6. If `delay` == 0, no delay information  will be stored
 * for the frame. This can be used when creating still (single-frame) GIF images.
 */
static void set_delay(ge_GIF *gif, uint16_t d)
{
  ssize_t res;

  res = write(gif->fd, (uint8_t[])
              {
              '!', 0xF9, 0x04, 0x04}, 4);
  OR_ABORT2;
  res = write_num(gif->fd, d);
  OR_ABORT2;
  res = write(gif->fd, "\0\0", 2);
  OR_ABORT2;
}

void ge_add_frame(ge_GIF *gif, uint16_t delay)
{
  uint16_t w, h, x, y;
  uint8_t *tmp;

  if (delay)
    set_delay(gif, delay);
  if (gif->nframes == 0)
  {
    w = gif->w;
    h = gif->h;
    x = y = 0;
  }
  else if (!get_bbox(gif, &w, &h, &x, &y))
  {
    /* image's not changed; save one pixel just to add delay */
    w = h = 1;
    x = y = 0;
  }
  put_image(gif, w, h, x, y);
  gif->nframes++;
  tmp = gif->back;
  gif->back = gif->frame;
  gif->frame = tmp;
}

void ge_close_gif(ge_GIF *gif)
{
  ssize_t res;

  res = write(gif->fd, ";", 1);
  OR_ABORT2;
  close(gif->fd);
  free(gif);
}
