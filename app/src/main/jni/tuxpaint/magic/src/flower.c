/*
  flower.c

  Flower Magic Tool Plugin
  Tux Paint - A simple drawing program for children.

  Copyright (c) 2002-2024 by Bill Kendrick and others; see AUTHORS.txt
  bill@newbreedsoftware.com
  https://tuxpaint.org/

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  (See COPYING.txt)

  Last updated: October 7, 2024
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "tp_magic_api.h"
#include "SDL_image.h"
#include "SDL_mixer.h"

/* Our globals: */

enum
{ SIDE_LEFT, SIDE_RIGHT };
enum
{ LEAFSIDE_RIGHT_DOWN,
  LEAFSIDE_LEFT_DOWN,
  LEAFSIDE_RIGHT_UP,
  LEAFSIDE_LEFT_UP
};

/* Note: We need to be able to have a default width of 32px when DEFAULT_SIZE
 * is chosen, to match legacy Flower tool's size */
#define NUM_SIZES 3
#define MAX_WIDTH 96
#define DEFAULT_SIZE (32 / (MAX_WIDTH / NUM_SIZES))

static int flower_cur_size = DEFAULT_SIZE;

static Mix_Chunk *flower_click_snd, *flower_release_snd;
static Uint8 flower_r, flower_g, flower_b;
static int flower_min_x, flower_max_x, flower_bottom_x, flower_bottom_y;
static int flower_side_first;
static int flower_side_decided;
static SDL_Surface *flower_base_full = NULL, *flower_leaf_full = NULL, *flower_petals_full = NULL;
static SDL_Surface *flower_base = NULL, *flower_leaf = NULL, *flower_petals = NULL, *flower_petals_colorized = NULL;

/* Local function prototypes: */

typedef struct
{
  float x, y;
} Point2D;

static void flower_drawbase(magic_api * api, SDL_Surface * canvas);
static void flower_drawflower(magic_api * api, SDL_Surface * canvas, int x, int y);
static Point2D flower_PointOnCubicBezier(Point2D * cp, float t);
static void flower_ComputeBezier(Point2D * cp, int numberOfPoints, Point2D * curve);
Uint32 flower_api_version(void);
int flower_init(magic_api * api, Uint8 disabled_features, Uint8 complexity_level);
int flower_get_tool_count(magic_api * api);
SDL_Surface *flower_get_icon(magic_api * api, int which);
char *flower_get_name(magic_api * api, int which);
int flower_get_group(magic_api * api, int which);
int flower_get_order(int which);
char *flower_get_description(magic_api * api, int which, int mode);
static void flower_predrag(magic_api * api, SDL_Surface * canvas, SDL_Surface * last, int ox, int oy, int x, int y);
void flower_drag(magic_api * api, int which, SDL_Surface * canvas,
                 SDL_Surface * last, int ox, int oy, int x, int y, SDL_Rect * update_rect);
void flower_click(magic_api * api, int which, int mode, SDL_Surface * canvas,
                  SDL_Surface * last, int x, int y, SDL_Rect * update_rect);
void flower_release(magic_api * api, int which, SDL_Surface * canvas,
                    SDL_Surface * last, int x, int y, SDL_Rect * update_rect);
static void flower_drawflower(magic_api * api, SDL_Surface * canvas, int x, int y);
static void flower_drawbase(magic_api * api, SDL_Surface * canvas);
static void flower_drawstalk(magic_api * api, SDL_Surface * canvas,
                             int top_x, int top_y, int minx, int maxx, int bottom_x, int bottom_y, int final);
void flower_shutdown(magic_api * api);
void flower_set_color(magic_api * api, int which, SDL_Surface * canvas,
                      SDL_Surface * last, Uint8 r, Uint8 g, Uint8 b, SDL_Rect * update_rect);
int flower_requires_colors(magic_api * api, int which);
static Point2D flower_PointOnCubicBezier(Point2D * cp, float t);
static void flower_ComputeBezier(Point2D * cp, int numberOfPoints, Point2D * curve);
static void flower_colorize_petals(magic_api * api);
void flower_switchin(magic_api * api, int which, int mode, SDL_Surface * canvas);
void flower_switchout(magic_api * api, int which, int mode, SDL_Surface * canvas);
int flower_modes(magic_api * api, int which);
Uint8 flower_accepted_sizes(magic_api * api, int which, int mode);
Uint8 flower_default_size(magic_api * api, int which, int mode);
void flower_set_size(magic_api * api, int which, int mode,
                     SDL_Surface * canvas, SDL_Surface * last, Uint8 size, SDL_Rect * update_rect);



Uint32 flower_api_version(void)
{
  return (TP_MAGIC_API_VERSION);
}


// No setup required:
int flower_init(magic_api *api, Uint8 disabled_features ATTRIBUTE_UNUSED, Uint8 complexity_level ATTRIBUTE_UNUSED)
{
  char fname[1024];
  SDL_Surface *tmp_surf;
  int h;

  snprintf(fname, sizeof(fname), "%ssounds/magic/flower_click.ogg", api->data_directory);
  flower_click_snd = Mix_LoadWAV(fname);

  snprintf(fname, sizeof(fname), "%ssounds/magic/flower_release.ogg", api->data_directory);
  flower_release_snd = Mix_LoadWAV(fname);

  snprintf(fname, sizeof(fname), "%simages/magic/flower_base.png", api->data_directory);
  tmp_surf = IMG_Load(fname);
  if (tmp_surf == NULL)
  {
    fprintf(stderr, "Cannot load %s\n", fname);
    return (0);
  }
  h = tmp_surf->h * MAX_WIDTH / tmp_surf->w;
  flower_base_full = api->scale(tmp_surf, MAX_WIDTH, h, 1);
  if (flower_base_full == NULL)
  {
    fprintf(stderr, "Cannot scale %s\n", fname);
    return (0);
  }

  snprintf(fname, sizeof(fname), "%simages/magic/flower_leaf.png", api->data_directory);
  tmp_surf = IMG_Load(fname);
  if (tmp_surf == NULL)
  {
    fprintf(stderr, "Cannot load %s\n", fname);
    return (0);
  }
  /* N.B.: Leaf is 1/2 as wide as base & petals */
  h = tmp_surf->h * (MAX_WIDTH / 2) / tmp_surf->w;
  flower_leaf_full = api->scale(tmp_surf, MAX_WIDTH / 2, h, 1);
  if (flower_leaf_full == NULL)
  {
    fprintf(stderr, "Cannot scale %s\n", fname);
    return (0);
  }

  snprintf(fname, sizeof(fname), "%simages/magic/flower_petals.png", api->data_directory);
  tmp_surf = IMG_Load(fname);
  if (tmp_surf == NULL)
  {
    fprintf(stderr, "Cannot load %s\n", fname);
    return (0);
  }
  h = tmp_surf->h * MAX_WIDTH / tmp_surf->w;
  flower_petals_full = api->scale(tmp_surf, MAX_WIDTH, h, 1);
  if (flower_petals_full == NULL)
  {
    fprintf(stderr, "Cannot scale %s\n", fname);
    return (0);
  }

  flower_cur_size = DEFAULT_SIZE;
  flower_set_size(api, 0, 0, NULL, NULL, flower_cur_size, NULL);

  return (1);
}

// We have multiple tools:
int flower_get_tool_count(magic_api *api ATTRIBUTE_UNUSED)
{
  return (1);
}

// Load our icons:
SDL_Surface *flower_get_icon(magic_api *api, int which ATTRIBUTE_UNUSED)
{
  char fname[1024];

  snprintf(fname, sizeof(fname), "%simages/magic/flower.png", api->data_directory);

  return (IMG_Load(fname));
}

// Return our names, localized:
char *flower_get_name(magic_api *api ATTRIBUTE_UNUSED, int which ATTRIBUTE_UNUSED)
{
  return (strdup(gettext("Flower")));
}

// Return our groups:
int flower_get_group(magic_api *api ATTRIBUTE_UNUSED, int which ATTRIBUTE_UNUSED)
{
  return MAGIC_TYPE_ARTISTIC;
}

// Return our orders:
int flower_get_order(int which ATTRIBUTE_UNUSED)
{
  return 100;
}

// Return our descriptions, localized:
char *flower_get_description(magic_api *api ATTRIBUTE_UNUSED, int which ATTRIBUTE_UNUSED, int mode ATTRIBUTE_UNUSED)
{
  return (strdup(gettext_noop("Click and drag to draw a flower stalk. Let go to finish the flower.")));
}

// Affect the canvas on drag:
static void flower_predrag(magic_api *api ATTRIBUTE_UNUSED,
                           SDL_Surface *canvas ATTRIBUTE_UNUSED,
                           SDL_Surface *last ATTRIBUTE_UNUSED, int ox, int oy, int x, int y)
{
  if (x < flower_min_x)
    flower_min_x = x;
  if (ox < flower_min_x)
    flower_min_x = ox;
  if (x > flower_max_x)
    flower_max_x = x;
  if (ox > flower_max_x)
    flower_max_x = ox;

  if (y > flower_bottom_y)
    y = flower_bottom_y;
  if (oy > flower_bottom_y)
    y = flower_bottom_y;

  // Determine which way to bend first:
  //
  if (flower_side_decided == 0)
  {
    if (x < flower_bottom_x - 10)
    {
      flower_side_first = SIDE_LEFT;
      flower_side_decided = 1;
    }
    else if (x > flower_bottom_x + 10)
    {
      flower_side_first = SIDE_RIGHT;
      flower_side_decided = 1;
    }
  }
}

void flower_drag(magic_api *api, int which ATTRIBUTE_UNUSED,
                 SDL_Surface *canvas, SDL_Surface *last, int ox, int oy, int x, int y, SDL_Rect *update_rect)
{
  flower_predrag(api, canvas, last, ox, oy, x, y);


  /* Erase any old stuff; this is a live-edited effect: */

  SDL_BlitSurface(last, NULL, canvas, NULL);


  /* Draw the base and the stalk (low-quality) for now: */

  flower_drawstalk(api, canvas,
                   x, y, flower_min_x, flower_max_x, flower_bottom_x, flower_bottom_y, !(api->button_down()));

  flower_drawbase(api, canvas);

  update_rect->x = 0;
  update_rect->y = 0;
  update_rect->w = canvas->w;
  update_rect->h = canvas->h;
}

// Affect the canvas on click:
void flower_click(magic_api *api, int which, int mode ATTRIBUTE_UNUSED,
                  SDL_Surface *canvas, SDL_Surface *last, int x, int y, SDL_Rect *update_rect)
{
  flower_min_x = x;
  flower_max_x = x;
  flower_bottom_x = x;
  flower_bottom_y = y - flower_base->h;

  flower_side_decided = 0;
  flower_side_first = SIDE_LEFT;

  flower_drag(api, which, canvas, last, x, y, x, y, update_rect);

  api->playsound(flower_click_snd, (x * 255) / canvas->w, 255);
}

// Affect the canvas on release:
void flower_release(magic_api *api, int which ATTRIBUTE_UNUSED,
                    SDL_Surface *canvas, SDL_Surface *last, int x, int y, SDL_Rect *update_rect)
{
  /* Don't let flower be too low compared to base: */

  if (y >= flower_bottom_y - flower_base->h)
    y = flower_bottom_y - flower_base->h;


  /* Do final calcs and draw base: */

  flower_predrag(api, canvas, last, x, y, x, y);


  /* Erase any old stuff: */

  SDL_BlitSurface(last, NULL, canvas, NULL);


  /* Draw high-quality stalk, and flower: */

  flower_drawstalk(api, canvas, x, y, flower_min_x, flower_max_x, flower_bottom_x, flower_bottom_y, 1);

  flower_drawflower(api, canvas, x, y);

  flower_drawbase(api, canvas);


  update_rect->x = 0;
  update_rect->y = 0;
  update_rect->w = canvas->w;
  update_rect->h = canvas->h;

  api->playsound(flower_release_snd, (x * 255) / canvas->w, 255);
}


static void flower_drawflower(magic_api *api ATTRIBUTE_UNUSED, SDL_Surface *canvas, int x, int y)
{
  SDL_Rect dest;

  if (flower_petals_colorized == NULL)  // Abort!
    return;

  dest.x = x - (flower_petals_colorized->w / 2);
  dest.y = y - (flower_petals_colorized->h / 2);

  SDL_BlitSurface(flower_petals_colorized, NULL, canvas, &dest);
}

static void flower_drawbase(magic_api *api ATTRIBUTE_UNUSED, SDL_Surface *canvas)
{
  SDL_Rect dest;

  if (flower_base == NULL)      // Abort!
    return;

  dest.x = flower_bottom_x - (flower_base->w / 2);
  dest.y = flower_bottom_y;

  SDL_BlitSurface(flower_base, NULL, canvas, &dest);
}

static void flower_drawstalk(magic_api *api ATTRIBUTE_UNUSED,
                             SDL_Surface *canvas, int top_x, int top_y,
                             int minx, int maxx, int bottom_x, int bottom_y, int final)
{
  Point2D control_points[4];
  Point2D *curve;
  int i, n_points;
  int left, right;
  SDL_Rect dest, src;
  int xx, yy, side;


  if (flower_leaf == NULL)      // Abort!
    return;

  /* Compute a nice bezier curve for the stalk, based on the
     base (x,y), leftmost (x), rightmost (x), and top (x,y) */

  control_points[0].x = top_x;
  control_points[0].y = top_y;

  if (flower_side_first == SIDE_LEFT)
  {
    control_points[1].x = minx;
    control_points[2].x = maxx;
  }
  else
  {
    control_points[1].x = maxx;
    control_points[2].x = minx;
  }

  control_points[1].y = ((bottom_y - top_y) / 3) + top_y;
  control_points[2].y = (((bottom_y - top_y) / 3) * 2) + top_y;

  control_points[3].x = bottom_x;
  control_points[3].y = bottom_y;

  if (final == 0)
    n_points = 8;
  else
    n_points = bottom_y - top_y;

  curve = (Point2D *) malloc(sizeof(Point2D) * n_points);

  flower_ComputeBezier(control_points, n_points, curve);


  /* Draw the curve: */

  for (i = 0; i < n_points - 1; i++)
  {
    if (final == 0)
    {
      dest.x = curve[i].x;
      dest.y = curve[i].y;
      dest.w = 4;
      dest.h = 4;
    }
    else
    {
      left = min(curve[i].x, curve[i + 1].x);
      right = max(curve[i].x, curve[i + 1].x);

      dest.x = left;
      dest.y = curve[i].y;
      dest.w = right - left + (flower_petals->w / 32) + 2;
      dest.h = 2 * ((flower_petals->w / 32) + 1);
    }

    SDL_FillRect(canvas, &dest, SDL_MapRGB(canvas->format, 42, 177, 42));


    /* When we're done (final render), we can add some random leaves: */

    if (final && i > flower_petals->h && i < n_points - flower_base->h
        && (i % (flower_leaf->h / 2)) == 0 && (rand() % 5) > 0)
    {
      int cx, cy;

      /* Check for hard left/right angles: */

      side = -1;

      if (curve[i - 2].x - curve[i + 2].x > 5)
      {
        /* Hard lower-left-to-upper-right (/),
           stick either a left-upward or right-downward facing leaf */

        if (rand() % 10 < 5)
          side = LEAFSIDE_LEFT_UP;
        else
          side = LEAFSIDE_RIGHT_DOWN;
      }
      else if (curve[i - 2].x - curve[i + 2].x < -5)
      {
        /* Hard lower-right-to-upper-left (\)
           stick either a right-upward or left-downward facing leaf */

        if (rand() % 10 < 5)
          side = LEAFSIDE_LEFT_DOWN;
        else
          side = LEAFSIDE_RIGHT_UP;
      }
      else if (fabs(curve[i - 2].x - curve[i + 2].x) < 5)
      {
        /* Mostly up; stick left- or right-downward: */

        if (rand() % 10 < 5)
          side = LEAFSIDE_LEFT_DOWN;
        else
          side = LEAFSIDE_RIGHT_DOWN;
      }


      /* Draw the appropriately-oriented leaf, if any: */

      cx = (left + right) / 2;
      cy = (curve[i].y);

      if (side == LEAFSIDE_RIGHT_DOWN)
      {
        dest.x = cx;
        dest.y = cy;

        SDL_BlitSurface(flower_leaf, NULL, canvas, &dest);
      }
      else if (side == LEAFSIDE_LEFT_DOWN)
      {
        for (xx = 0; xx < flower_leaf->w; xx++)
        {
          src.x = xx;
          src.y = 0;
          src.w = 1;
          src.h = flower_leaf->h;

          dest.x = cx - xx;
          dest.y = cy;

          SDL_BlitSurface(flower_leaf, &src, canvas, &dest);
        }
      }
      else if (side == LEAFSIDE_RIGHT_UP)
      {
        for (yy = 0; yy < flower_leaf->h; yy++)
        {
          src.x = 0;
          src.y = yy;
          src.w = flower_leaf->w;
          src.h = 1;

          dest.x = cx;
          dest.y = cy - yy;

          SDL_BlitSurface(flower_leaf, &src, canvas, &dest);
        }
      }
      else if (side == LEAFSIDE_LEFT_UP)
      {
        for (xx = 0; xx < flower_leaf->w; xx++)
        {
          for (yy = 0; yy < flower_leaf->h; yy++)
          {
            src.x = xx;
            src.y = yy;
            src.w = 1;
            src.h = 1;

            dest.x = cx - xx;
            dest.y = cy - yy;

            SDL_BlitSurface(flower_leaf, &src, canvas, &dest);
          }
        }
      }
    }
  }

  free(curve);
}

void flower_shutdown(magic_api *api ATTRIBUTE_UNUSED)
{
  if (flower_click_snd != NULL)
    Mix_FreeChunk(flower_click_snd);

  if (flower_release_snd != NULL)
    Mix_FreeChunk(flower_release_snd);

  if (flower_base != NULL)
    SDL_FreeSurface(flower_base);
  if (flower_leaf != NULL)
    SDL_FreeSurface(flower_leaf);
  if (flower_petals != NULL)
    SDL_FreeSurface(flower_petals);
  if (flower_petals_colorized != NULL)
    SDL_FreeSurface(flower_petals_colorized);

  if (flower_base_full != NULL)
    SDL_FreeSurface(flower_base_full);
  if (flower_leaf_full != NULL)
    SDL_FreeSurface(flower_leaf_full);
  if (flower_petals_full != NULL)
    SDL_FreeSurface(flower_petals_full);
}

// Record the color from Tux Paint:
void flower_set_color(magic_api *api, int which ATTRIBUTE_UNUSED,
                      SDL_Surface *canvas ATTRIBUTE_UNUSED,
                      SDL_Surface *last ATTRIBUTE_UNUSED, Uint8 r, Uint8 g,
                      Uint8 b, SDL_Rect *update_rect ATTRIBUTE_UNUSED)
{
  flower_r = r;
  flower_g = g;
  flower_b = b;

  flower_colorize_petals(api);
}

// Use colors:
int flower_requires_colors(magic_api *api ATTRIBUTE_UNUSED, int which ATTRIBUTE_UNUSED)
{
  return 1;
}



/*
Code to generate a cubic Bezier curve
*/

/*
cp is a 4 element array where:
cp[0] is the starting point, or P0 in the above diagram
cp[1] is the first control point, or P1 in the above diagram
cp[2] is the second control point, or P2 in the above diagram
cp[3] is the end point, or P3 in the above diagram
t is the parameter value, 0 <= t <= 1
*/

static Point2D flower_PointOnCubicBezier(Point2D *cp, float t)
{
  float ax, bx, cx;
  float ay, by, cy;
  float tSquared, tCubed;
  Point2D result;

  /* calculate the polynomial coefficients */

  cx = 3.0 * (cp[1].x - cp[0].x);
  bx = 3.0 * (cp[2].x - cp[1].x) - cx;
  ax = cp[3].x - cp[0].x - cx - bx;

  cy = 3.0 * (cp[1].y - cp[0].y);
  by = 3.0 * (cp[2].y - cp[1].y) - cy;
  ay = cp[3].y - cp[0].y - cy - by;

  /* calculate the curve point at parameter value t */

  tSquared = t * t;
  tCubed = tSquared * t;

  result.x = (ax * tCubed) + (bx * tSquared) + (cx * t) + cp[0].x;
  result.y = (ay * tCubed) + (by * tSquared) + (cy * t) + cp[0].y;

  return result;
}

/*
 ComputeBezier fills an array of Point2D structs with the curve   
 points generated from the control points cp. Caller must 
 allocate sufficient memory for the result, which is 
 <sizeof(Point2D) numberOfPoints>
*/

static void flower_ComputeBezier(Point2D *cp, int numberOfPoints, Point2D *curve)
{
  float dt;
  int i;

  dt = 1.0 / (numberOfPoints - 1);

  for (i = 0; i < numberOfPoints; i++)
    curve[i] = flower_PointOnCubicBezier(cp, i * dt);
}


static void flower_colorize_petals(magic_api *api)
{
  Uint32 amask;
  int x, y;
  Uint8 r, g, b, a;

  if (flower_petals_colorized != NULL)
    SDL_FreeSurface(flower_petals_colorized);

  if (flower_petals == NULL)    // Abort!
    return;

  /* Create a surface to render into: */

  amask = ~(flower_petals->format->Rmask | flower_petals->format->Gmask | flower_petals->format->Bmask);

  flower_petals_colorized =
    SDL_CreateRGBSurface(SDL_SWSURFACE,
                         flower_petals->w,
                         flower_petals->h,
                         flower_petals->format->BitsPerPixel,
                         flower_petals->format->Rmask,
                         flower_petals->format->Gmask, flower_petals->format->Bmask, amask);

  /* Render the new petals: */

  SDL_LockSurface(flower_petals);
  SDL_LockSurface(flower_petals_colorized);

  for (y = 0; y < flower_petals->h; y++)
  {
    for (x = 0; x < flower_petals->w; x++)
    {
      SDL_GetRGBA(api->getpixel(flower_petals, x, y), flower_petals->format, &r, &g, &b, &a);

      api->putpixel(flower_petals_colorized, x, y,
                    SDL_MapRGBA(flower_petals_colorized->format, flower_r, flower_g, flower_b, a));

      if (api->in_circle((x - flower_petals->w / 2), (y - flower_petals->h / 2), (flower_petals->w / 4)))
      {
        api->putpixel(flower_petals_colorized, x, y, SDL_MapRGBA(flower_petals_colorized->format, 0xFF, 0xFF, 0x00, a));
      }
    }
  }

  SDL_UnlockSurface(flower_petals_colorized);
  SDL_UnlockSurface(flower_petals);
}

void flower_switchin(magic_api *api ATTRIBUTE_UNUSED,
                     int which ATTRIBUTE_UNUSED, int mode ATTRIBUTE_UNUSED, SDL_Surface *canvas ATTRIBUTE_UNUSED)
{
}

void flower_switchout(magic_api *api ATTRIBUTE_UNUSED,
                      int which ATTRIBUTE_UNUSED, int mode ATTRIBUTE_UNUSED, SDL_Surface *canvas ATTRIBUTE_UNUSED)
{
}

int flower_modes(magic_api *api ATTRIBUTE_UNUSED, int which ATTRIBUTE_UNUSED)
{
  return (MODE_PAINT_WITH_PREVIEW);
}

Uint8 flower_accepted_sizes(magic_api *api ATTRIBUTE_UNUSED, int which ATTRIBUTE_UNUSED, int mode ATTRIBUTE_UNUSED)
{
  return NUM_SIZES;
}

Uint8 flower_default_size(magic_api *api ATTRIBUTE_UNUSED, int which ATTRIBUTE_UNUSED, int mode ATTRIBUTE_UNUSED)
{
  return DEFAULT_SIZE;
}

void flower_set_size(magic_api *api, int which ATTRIBUTE_UNUSED,
                     int mode ATTRIBUTE_UNUSED,
                     SDL_Surface *canvas ATTRIBUTE_UNUSED,
                     SDL_Surface *last ATTRIBUTE_UNUSED, Uint8 size, SDL_Rect *update_rect ATTRIBUTE_UNUSED)
{
  int scale, width, height;

  flower_cur_size = size;
  scale = (size * 100) / NUM_SIZES;

  width = (scale * flower_base_full->w) / 100;
  height = (scale * flower_base_full->h) / 100;
  if (flower_base != NULL)
    SDL_FreeSurface(flower_base);
  flower_base = api->scale(flower_base_full, width, height, 1);

  width = (scale * flower_leaf_full->w) / 100;
  height = (scale * flower_leaf_full->h) / 100;
  if (flower_leaf != NULL)
    SDL_FreeSurface(flower_leaf);
  flower_leaf = api->scale(flower_leaf_full, width, height, 1);

  width = (scale * flower_petals_full->w) / 100;
  height = (scale * flower_petals_full->h) / 100;
  if (flower_petals != NULL)
    SDL_FreeSurface(flower_petals);
  flower_petals = api->scale(flower_petals_full, width, height, 1);

  flower_colorize_petals(api);
}
