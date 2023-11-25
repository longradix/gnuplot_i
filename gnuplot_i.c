#ifndef _GNUPLOT_PIPES_C_
#define _GNUPLOT_PIPES_C_

/*-------------------------------------------------------------------------*/
/**
  @file    gnuplot_i.c
  @brief   C interface to gnuplot.

  gnuplot_i is a C interface library that enables sending display
  requests to gnuplot through C calls. gnuplot itself is an open source
  plotting library also written in C.

  The plot can be displayed in its own window or otherwise saved as an image file
  to disk.

  Example of a minimal program structure:

  @code
    gnuplot_ctrl *handle = gnuplot_init();
    gnuplot_cmd (handle, "set terminal png");
    gnuplot_plot_equation (handle, "sin(x)", "Sine wave");
    gnuplot_close (handle);
  @endcode

*/
/*--------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
  Includes
 ---------------------------------------------------------------------------*/

#include "gnuplot_i.h"

/*---------------------------------------------------------------------------
  Defines
 ---------------------------------------------------------------------------*/

/** Maximum amount of characters of a gnuplot command. */
#define GP_CMD_SIZE 2048
/** Maximum amount of characters of a plot title. */
#define GP_TITLE_SIZE 80
/** Maximum amount of characters for an equation y=f(x). */
#define GP_EQ_SIZE 512
/** Maximum amount of characters of a temporary file name. */
#define NAME_SIZE 128
/** Maximum amount of characters of a name in the PATH. */
#define PATH_MAXNAMESZ 4096
/** Error message display. */
#define FAIL_IF(EXP, MSG) ({ if (EXP) { printf("ERROR: " MSG "\n"); exit(EXIT_FAILURE); }})

#ifdef _WIN32
#undef P_tmpdir
#endif
/** Define `P_tmpdir` if not defined; this is normally a POSIX symbol. */
#ifndef P_tmpdir
#define P_tmpdir "."
#endif

#define GNUPLOT_TEMPFILE "%s/gnuplot-i-XXXXXX"
#ifndef _WIN32
#define GNUPLOT_EXEC "gnuplot"
#else
#define GNUPLOT_EXEC "pgnuplot.exe"
#endif

/*---------------------------------------------------------------------------
  Function codes
 ---------------------------------------------------------------------------*/

#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>

/*-------------------------------------------------------------------------*/
/**
  @brief    Create temporary output pipe with randomised name.
  @param    name Name of the pipe.
  @return   int
 */
/*-------------------------------------------------------------------------*/

int mkstemp (char *name) {
  srand(time(NULL));
  int i;
  char *start = strstr(name, "XXXXXX");

  for (i = 0; i < 6; i++) {
    start[i] = (char)(48 + ((int)rand() * 10 / 32768.0));
  }
  i = open(name, O_RDWR | O_CREAT);
  if (i != -1) {
    DWORD dwFileAttr = GetFileAttributes(name);
    SetFileAttributes(name, dwFileAttr & !FILE_ATTRIBUTE_READONLY);
  }
  return i;
}
#endif

/*-------------------------------------------------------------------------*/
/**
  @brief    Find out where a command lives in the PATH.
  @param    pname Name of the program to look for.
  @return   pointer to statically allocated character string.

  This is the C equivalent to the 'which' command in Unix. It parses
  the PATH environment variable to find out where a command lives.
  The returned character string is statically allocated within this function,
  i.e. there is no need to free it.
  Note that the contents of this string will change from one call to the next,
  similar to all static variables in a function.

  The input character string must be the name of a command without
  prefixing path of any kind, i.e. only the command name. The returned
  string is the path in which a command matching the same name was found.

  Examples:

  @code
    gnuplot_get_program_path("hello") returns "."
    gnuplot_get_program_path("ls") returns "/bin"
    gnuplot_get_program_path("csh") returns "/usr/bin"
    gnuplot_get_program_path("/bin/ls") returns NULL
  @endcode

 */
/*-------------------------------------------------------------------------*/

char *gnuplot_get_program_path (char *pname) {
  int i, j, lg;
  char *path;
  static char buf[PATH_MAXNAMESZ];

  /* Trivial case: try in current working directory */
  sprintf(buf, "./%s", pname);
  if (access(buf, X_OK) == 0) {
    sprintf(buf, ".");
    return buf;
  }

  /* Try in all paths given in the PATH variable */
  buf[0] = 0;
  path = getenv("PATH");
  FAIL_IF (path == NULL, "The PATH variable has not been set");
  for (i = 0; path[i];) {
    for (j = i; (path[j]) && (path[j] != ':'); j++);
    lg = j - i;
    strncpy(buf, path + i, lg);
    if (lg == 0) buf[lg++] = '.';
    buf[lg++] = '/';
    strcpy(buf + lg, pname);
    if (access(buf, X_OK) == 0) /* Found it! */ break;
    buf[0] = 0;
    i = j;
    if (path[i] == ':') i++;
  }

  /* If the buffer is still empty, then the command was not found */
  FAIL_IF (buf[0] == 0, "Command not found in PATH variable");

  /* Otherwise truncate the command name to yield path only */
  lg = strlen(buf) - 1;
  while (buf[lg] != '/') {
    buf[lg] = 0;
    lg --;
  }
  buf[lg] = 0;
  return buf;
}

/*-------------------------------------------------------------------------*/
/**
  @brief     Open a gnuplot session, ready to receive commands.
  @return    Newly allocated gnuplot control structure.

  This opens a new gnuplot session, ready for input.
  The struct controlling a gnuplot session should remain opaque and only be
  accessed through the provided functions.

  The session must be closed using gnuplot_close().
 */
/*--------------------------------------------------------------------------*/

gnuplot_ctrl *gnuplot_init (void) {
  gnuplot_ctrl *handle;
#ifndef _WIN32
#ifndef __APPLE__
  FAIL_IF (getenv("DISPLAY") == NULL, "Cannot find DISPLAY variable");
#endif
#endif
#ifndef _WIN32
  FAIL_IF (gnuplot_get_program_path("gnuplot") == NULL, "Cannot find gnuplot in your PATH, check `which gnuplot`");
#endif
  FAIL_IF (gnuplot_get_program_path(GNUPLOT_EXEC) == NULL, "Cannot find gnuplot in your PATH, check `which gnuplot`");

  /* Structure initialization: */
  handle = (gnuplot_ctrl *)malloc(sizeof(gnuplot_ctrl));
  handle->nplots = 0;
  gnuplot_setstyle(handle, "points");
  handle->ntmp = 0;
  handle->gnucmd = popen(GNUPLOT_EXEC, "w");
  if (handle->gnucmd == NULL) {
    free(handle);
    FAIL_IF (0 == 0, "Error starting gnuplot");
  }

  /* Set plot dimensions (should be handled elsewhere, but just to get things going) */
  int width = 900;
  int height = 400;

  /* Set terminal output type */
#ifdef _WIN32
  gnuplot_setterm(handle, "windows", width, height);
#elif __APPLE__
  /* Determine whether to use aqua or x11 as the default */
  if (getenv("DISPLAY") == NULL || (getenv("USE_AQUA") != NULL && strcmp(getenv("USE_AQUA"), "1") >= 0))
    gnuplot_setterm(handle, "aqua", width, height);
  else
    gnuplot_setterm(handle, "x11", width, height);
#else
  /* The default is wxt, but this requires wxWidgets to be installed (need a test for that) */
  gnuplot_setterm(handle, "wxt", width, height);
#endif
  return handle;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Print contents of gnuplot control handle to screen.
  @param    handle    Gnuplot session control handle

  This is for debugging purposes only.
 */
/*--------------------------------------------------------------------------*/

void print_gnuplot_handle (gnuplot_ctrl *handle) {
  //fprintf(gnucmd);   /* Pipe to gnuplot process. For debugging purposes only. */
  printf("Temporary files: %d\n", handle->ntmp);    /* Number of temporary files in the current session. */
  printf("Active plots: %d\n", handle->nplots);     /* Number of currently active plots. */
  printf("Plotting style: %s\n", handle->pstyle);   /* Current plotting style. */
  printf("Terminal name: %s\n", handle->term);      /* Save terminal name (used by `gnuplot_hardcopy()` function). */
  //char to_delete[GP_MAX_TMP_FILES][GP_TMP_NAME_SIZE];   /* Names of temporary files. Only relevant for multiplots. */
  return;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Close a gnuplot session previously opened by gnuplot_init()
  @param    handle    Gnuplot session control handle.
  @return   void

  Kills the child process-ID (PID) and deletes all opened temporary files.
  It is mandatory to call this function to close the handle, otherwise
  temporary files are not cleaned from memory and the child process might
  survive and unable to be reused.
 */
/*--------------------------------------------------------------------------*/

void gnuplot_close (gnuplot_ctrl *handle) {
  FAIL_IF (pclose(handle->gnucmd) == -1, "Cannot close communication to gnuplot");
  if (handle->ntmp) {
    for (int i = 0; i < handle->ntmp; i++) {
#ifdef _WIN32
      int x = remove(handle->to_delete[i]);
      if (x) printf("Cannot delete %s: error number %d\n", handle->to_delete[i], errno);
#else
      remove(handle->to_delete[i]);
#endif
    }
  }
  free(handle);
  return;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Send a command to an active gnuplot session.
  @param    handle    Gnuplot session control handle
  @param    cmd       Command to send, same as a printf statement.

  This sends a string to an active gnuplot session to be executed.
  This function is the fallback option: if a certain functionality is not
  supported by one of the functions, it is in most cases possible to send
  the required gnuplot commands using this function.

  The command syntax is the same as printf.

  Examples:

  @code
    gnuplot_cmd(h, "plot %d*x", 23.0);
    gnuplot_cmd(h, "plot %g * cos(%g * x)", 32.0, -3.0);
  @endcode

  Since the communication to the gnuplot process is run through a standard Unix
  pipe, it is only unidirectional. This means that it is not possible for this
  interface to query an error status back from gnuplot.
 */
/*--------------------------------------------------------------------------*/

void gnuplot_cmd (gnuplot_ctrl *handle, char *cmd, ...) {
  va_list ap;
  char local_cmd[GP_CMD_SIZE];

  va_start(ap, cmd);
  vsprintf(local_cmd, cmd, ap);
  va_end(ap);
  strcat(local_cmd, "\n");
  fputs(local_cmd, handle->gnucmd);
  fflush(handle->gnucmd);
  return;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Change the plotting style of a gnuplot session.
  @param    handle      Gnuplot session control handle
  @param    plot_style  Plotting-style (character string)
  @return   void

  The provided plotting style is one of the following character strings:
  - lines
  - points
  - linespoints
  - impulses
  - dots
  - steps
  - errorbars (superseded by xerrorbars and xyerrorbars since version 5.0)
  - boxes
  - boxerrorbars
 */
/*--------------------------------------------------------------------------*/

void gnuplot_setstyle (gnuplot_ctrl *handle, char *plot_style) {
  if (strcmp(plot_style, "lines") &&
      strcmp(plot_style, "points") &&
      strcmp(plot_style, "linespoints") &&
      strcmp(plot_style, "impulses") &&
      strcmp(plot_style, "dots") &&
      strcmp(plot_style, "steps") &&
      strcmp(plot_style, "filledcurves") &&
      strcmp(plot_style, "errorbars") &&
      strcmp(plot_style, "boxes") &&
      strcmp(plot_style, "boxerrorbars")) {
    fprintf(stderr, "Warning: unknown requested plot style: using default 'points'\n");
    strcpy(handle->pstyle, "points");
  } else {
    strcpy(handle->pstyle, plot_style);
  }
  return;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Change the terminal of a gnuplot session.
  @param    handle    Gnuplot session control handle
  @param    terminal  Terminal name (character string)
  @return   void

  In gnuplot the terminal type is the output channel to which the plot should be
  displayed on.

  The terminal type should be one of the following character strings:
  - `x11` for Linux, no anti-aliasing (default)
  - `wxt` or `qt` for Linux, with anti-aliasing
  - `aqua` for OSX
  - `wxt` or `windows` for MS-Windows.

  No check is made on the validity of the terminal name. This function
  calls `gnuplot_cmd` with the provided terminal name. If this function is not
  called, then the `x11` terminal type will be used.
 */
/*--------------------------------------------------------------------------*/

void gnuplot_setterm (gnuplot_ctrl *handle, char *terminal, int width, int height) {
  char cmd[GP_CMD_SIZE];

  strncpy(handle->term, terminal, 32);
  handle->term[31] = 0;
  FAIL_IF (width < 0 || height < 0, "Plot size dimensions cannot be negative");
  sprintf(cmd, "set terminal %s size %d,%d", handle->term, width, height);
  gnuplot_cmd(handle, cmd);
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Set the axis label of a gnuplot session.
  @param    handle  Gnuplot session control handle.
  @param    label   Character string to use for axis label.
  @param    axis    Character string to identify axis, ie "x", "y" or "z".
  @return   void

  Sets the axis label for a gnuplot session.

  Example:

  @code
    gnuplot_set_axislabel(h, "x", "Time(sec)");
  @endcode
 */
/*--------------------------------------------------------------------------*/

void gnuplot_set_axislabel (gnuplot_ctrl *handle, char *axis, char *label) {
  char cmd[GP_CMD_SIZE];

  sprintf(cmd, "set %slabel \"%s\"", axis, label);
  gnuplot_cmd(handle, cmd);
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Reset a gnuplot session (next plot will erase previous ones).
  @param    handle Gnuplot session control handle.
  @return   void

  Resets a gnuplot session, i.e. the next plot will erase all previous ones.
  This function can effectively be used to insert a new plot in the same
  window with all options maintained. As such, it can be used to create
  animations.
 */
/*--------------------------------------------------------------------------*/

void gnuplot_resetplot (gnuplot_ctrl *handle) {
  if (handle->ntmp) {
    for (int i = 0; i < handle->ntmp; i++) {
      remove(handle->to_delete[i]);
    }
  }
  handle->ntmp = 0;
  handle->nplots = 0;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Check for standard errors and exit when encountered.
  @param    handle Gnuplot session control handle.
  @return   void

  This is for refactoring purposes only.
 */
/*--------------------------------------------------------------------------*/

void gnuplot_i_error (gnuplot_ctrl *handle) {
  FAIL_IF (handle == NULL, "Gnuplot_i control handle invalid");
  FAIL_IF (handle->nplots > 0, "A gnuplot session is already open and held by another process");
  FAIL_IF (handle->ntmp == GP_MAX_TMP_FILES - 1, "Maximum number of temporary files reached: cannot open more");
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Plot a 2d graph from a list of points.
  @param    handle    Gnuplot session control handle.
  @param    x         Pointer to a list of x coordinates.
  @param    y         Pointer to a list of y coordinates (can be NULL).
  @param    n         Number of doubles in x (assumed the same as in y).
  @param    title     Title of the plot (can be NULL).
  @return   void

  Plots a 2d graph from a list of coordinates of type double.

  Provide points through a list of x and a list of y coordinates, with the
  following proviso.
  * If y is NULL, then the x-coordinate is the index of the value in the list,
  and the y coordinate is the value in the list.
  * If y is not NULL, then both arrays are assumed to contain the same number of
  values.

  Example:

  @code
    gnuplot_ctrl *h;
    double x[50], y[50];

    h = gnuplot_init();
    for (int i = 0; i < 50; i++) {
      x[i] = (double)(i)/10.0;
      y[i] = x[i] * x[i];
    }
    gnuplot_plot_coordinates(h, x, y, 50, "parabola");
    sleep(2);
    gnuplot_close(h);
  @endcode
 */
/*--------------------------------------------------------------------------*/

void gnuplot_plot_coordinates (gnuplot_ctrl *handle, double *x, double *y, int n, char *title) {
  int tmpfd;
  char name[NAME_SIZE];
  char cmd[GP_CMD_SIZE];

  /* Error handling: mandatory arguments, already open session, opening temporary file */
  FAIL_IF (x == NULL || (n < 1), "One of the parameters to gnuplot_plot_coordinates() has been misspecified");
  gnuplot_i_error(handle);

  /* Open temporary file for output */
  sprintf(name, GNUPLOT_TEMPFILE, P_tmpdir);
  FAIL_IF ((tmpfd = mkstemp(name)) == -1, "Cannot create temporary file: exiting gnuplot_plot_coordinates()");

  /* Store file name in array for future deletion */
  strcpy(handle->to_delete[handle->ntmp], name);
  handle->ntmp++;

  /* Write data to this file */
  for (int i = 0; i < n; i++) {
    (y == NULL || memcmp(y, y+1, (sizeof(y)-1)*sizeof(y[0])) == 0) ? sprintf(cmd, "%g\n", x[i]) : sprintf(cmd, "%g %g\n", x[i], y[i]);
    write(tmpfd, cmd, strlen(cmd));
  }
  close(tmpfd);

  /* Command to be sent to gnuplot */
  sprintf(cmd, "%s \"%s\" title \"%s\" with %s", (handle->nplots > 0) ? "replot" : "plot", name, (title) ? title : "No title" , handle->pstyle);
  gnuplot_cmd(handle, cmd);
  handle->nplots++;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Plot a 3d graph (surface plot) from a list of points.
  @param    handle    Gnuplot session control handle.
  @param    x         Pointer to a list of x coordinates.
  @param    y         Pointer to a list of y coordinates.
  @param    z         Pointer to a list of z coordinates.
  @param    n         Number of doubles in x (same for y and z).
  @param    title     Title of the plot (can be NULL).
  @return   void

  Plots a 3d graph from a list of points, passed as arrays x, y and z.
  All arrays are assumed to contain the same number of values.

  Based on `gnuplot_plot_coordinates`, modifications by Robert Bradley 12/10/2004

  Example:

  @code
    gnuplot_ctrl *h;
    double x[50], y[50], z[50];

    h = gnuplot_init();
    for (int i = 0; i < 50; i++) {
      x[i] = (double)(i)/10.0;
      y[i] = x[i] * x[i];
      z[i] = x[i] * x[i]/2.0;
    }
    gnuplot_splot(h, x, y, z, 50, "parabola");
    sleep(2);
    gnuplot_close(h);
  @endcode
 */
/*--------------------------------------------------------------------------*/

void gnuplot_splot (gnuplot_ctrl *handle, double *x, double *y, double *z, int n, char *title) {
  int tmpfd;
  char name[NAME_SIZE];
  char cmd[GP_CMD_SIZE];

  /* Error handling: mandatory arguments, already open session, opening temporary file */
  FAIL_IF (x == NULL || y == NULL || (n < 1), "One of the parameters to gnuplot_splot() has been misspecified");
  gnuplot_i_error(handle);

  /* Open temporary file for output */
  sprintf(name, GNUPLOT_TEMPFILE, P_tmpdir);
  FAIL_IF ((tmpfd = mkstemp(name)) == -1, "Cannot create temporary file: exiting gnuplot_splot()");

  /* Store file name in array for future deletion */
  strcpy(handle->to_delete[handle->ntmp], name);
  handle->ntmp++;

  /* Write data to this file */
  for (int i = 0; i < n; i++) {
    sprintf(cmd, "%g %g %g\n", x[i], y[i], z[i]);
    write(tmpfd, cmd, strlen(cmd));
  }
  close(tmpfd);

  /* Command to be sent to gnuplot */
  sprintf(cmd, "splot \"%s\" title \"%s\" with %s", name, (title) ? title : "No title", handle->pstyle);
  gnuplot_cmd(handle, cmd);
  handle->nplots++;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Plot a 3d graph (surface plot) from a grid of points.
  @param    handle    Gnuplot session control handle.
  @param    points    Pointer to a grid of points (rows,cols).
  @param    rows      Number of rows (y points).
  @param    cols      Number of columns (x points).
  @param    title     Title of the plot (can be NULL).
  @return   void

  Plots a 3d graph from a grid of points, passed in the form of an array [x,y].

  Based on `gnuplot_splot`, modifications by Robert Bradley 2/4/2006

  Example:

  @code
    gnuplot_splot_grid(handle, (double*) points, rows, cols, title);
  @endcode
 */
/*--------------------------------------------------------------------------*/

void gnuplot_splot_grid (gnuplot_ctrl *handle, double *points, int rows, int cols, char *title) {
  int tmpfd;
  char name[NAME_SIZE];
  char cmd[GP_CMD_SIZE];

  /* Error handling: mandatory arguments, already open session, opening temporary file */
  FAIL_IF (points == NULL || (rows < 1) || (cols < 1), "One of the parameters to gnuplot_splot_grid() has been misspecified");
  gnuplot_i_error(handle);

  /* Open temporary file for output */
  sprintf(name, GNUPLOT_TEMPFILE, P_tmpdir);
  FAIL_IF ((tmpfd = mkstemp(name)) == -1, "Cannot create temporary file: exiting gnuplot_splot_grid()");

  /* Store file name in array for future deletion */
  strcpy(handle->to_delete[handle->ntmp], name);
  handle->ntmp++;

  /* Write data to this file */
  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
      sprintf(cmd, "%d %d %g\n", i, j, points[i * cols + j]);
      write(tmpfd, cmd, strlen(cmd));
    }
    strcpy(cmd, "\n");
    write(tmpfd, cmd, strlen(cmd));
  }
  close(tmpfd);

  /* Command to be sent to gnuplot */
  sprintf(cmd, "splot \"%s\" title \"%s\" with %s", name, (title) ? title : "No title", handle->pstyle);
  gnuplot_cmd(handle, cmd);
  handle->nplots++;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Plot contours from a list of points.
  @param    handle    Gnuplot session control handle.
  @param    x         Pointer to a list of x coordinates (length = nx*ny).
  @param    y         Pointer to a list of y coordinates (length = nx*ny).
  @param    z         Pointer to a list of z coordinates (length = nx*ny).
  @param    nx        Number of doubles in x-direction.
  @param    ny        Number of doubles in y-direction.
  @param    title     Title of the plot (can be NULL).
  @return   void

  Plots a contour plot from a list of points, passed as arrays x, y and z.

  Based on `gnuplot_splot`, modifications by Robert Bradley 23/11/2005

  Example:

  @code
    gnuplot_ctrl *h;

    h = gnuplot_init();
    int count = 50;
    double x[count*count], y[count*count], z[count*count];
    for (int i = 0; i < count; i++) {
      for (int j = 0; j < count; j++) {
        x[count*i+j] = i;
        y[count*i+j] = j;
        z[count*i+j] = 1000*sqrt(pow(i-count/2, 2)+pow(j-count/2, 2));
      }
    }
    gnuplot_setstyle(h, "lines");
    gnuplot_contour_plot(h, x, y, z, count, count, "Points");
    sleep(2);
    gnuplot_close(h);
  @endcode
 */
/*--------------------------------------------------------------------------*/

void gnuplot_contour_plot (gnuplot_ctrl *handle, double *x, double *y, double *z, int nx, int ny, char *title) {
  int tmpfd;
  char name[NAME_SIZE];
  char cmd[GP_CMD_SIZE];

  /* Error handling: mandatory arguments, already open session, opening temporary file */
  FAIL_IF (x == NULL || y == NULL || (nx < 1) || (ny < 1), "One of the parameters to gnuplot_contour_plot() has been misspecified");
  gnuplot_i_error(handle);

  /* Open temporary file for output */
  sprintf(name, GNUPLOT_TEMPFILE, P_tmpdir);
  FAIL_IF ((tmpfd = mkstemp(name)) == -1, "Cannot create temporary file: exiting gnuplot_contour_plot()");

  /* Store file name in array for future deletion */
  strcpy(handle->to_delete[handle->ntmp], name);
  handle->ntmp++;

  /* Write data to this file */
  for (int i = 0; i < nx; i++) {
    for (int j = 0; j < ny; j++) {
      sprintf(cmd, "%g %g %g\n", x[nx * i + j], y[nx * i + j], z[nx * i + j]);
      write(tmpfd, cmd, strlen(cmd));
    }
    sprintf(cmd, "\n");
    write(tmpfd, cmd, strlen(cmd));
  }
  close(tmpfd);

  /* Command to be sent to gnuplot */
  gnuplot_cmd(handle, "unset surface");
  gnuplot_cmd(handle, "set contour base");
  gnuplot_cmd(handle, "set view map");
  gnuplot_cmd(handle, "set view 0,0");

  /* Command to be sent to gnuplot */
  sprintf(cmd, "splot \"%s\" title \"%s\" with %s", name, (title) ? title : "No title", handle->pstyle);
  gnuplot_cmd(handle, cmd);
  handle->nplots++;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Plot a 3d graph using callback functions to return the points.
  @param    handle    Gnuplot session control handle.
  @param    obj       Pointer to an arbitrary object.
  @param    getPoint  Pointer to a callback function.
  @param    n         Number of doubles in x (y and z must be the the same).
  @param    title     Title of the plot (can be NULL).
  @return   void

  Callback:

  void getPoint(void *object, gnuplot_point *point, int index, int pointCount);
    @param    obj     Pointer to an arbitrary object
    @param    point   Pointer to the returned point struct (double x, y, z)
    @param    i       Index of the current point (0 to n-1)
    @param    n       Number of points
    @return   void
 */
/*--------------------------------------------------------------------------*/

void gnuplot_splot_obj (gnuplot_ctrl *handle, void *obj, void (*getPoint)(void *, gnuplot_point *, int, int), int n, char *title) {
  int tmpfd;
  char name[NAME_SIZE];
  char cmd[GP_CMD_SIZE];

  /* Error handling: mandatory arguments, already open session, opening temporary file */
  FAIL_IF (getPoint == NULL || (n < 1), "One of the parameters to gnuplot_splot_obj() has been misspecified");
  gnuplot_i_error(handle);

  /* Open temporary file for output */
  sprintf(name, GNUPLOT_TEMPFILE, P_tmpdir);
  FAIL_IF ((tmpfd = mkstemp(name)) == -1, "Cannot create temporary file: exiting gnuplot_splot_obj()");

  /* Store file name in array for future deletion */
  strcpy(handle->to_delete[handle->ntmp], name);
  handle->ntmp++;

  /* Write data to this file */
  gnuplot_point point;
  for (int i = 0; i < n; i++) {
    getPoint(obj, &point, i, n);
    sprintf(cmd, "%g %g %g\n", point.x, point.y, point.z);
    write(tmpfd, cmd, strlen(cmd));
  }
  close(tmpfd);

  /* Command to be sent to gnuplot */
  sprintf(cmd, "splot \"%s\" title \"%s\" with %s", name, (title) ? title : "No title", handle->pstyle);
  gnuplot_cmd(handle, cmd);
  handle->nplots++;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Plot a 2d graph using a callback function to return points.
  @param    handle    Gnuplot session control handle.
  @param    obj       Pointer to an arbitrary object.
  @param    getPoint  Pointer to a callback function.
  @param    n         Number of points.
  @param    title     Title of the plot (can be NULL).
  @return   void

  The callback function is of the following form, and is called once for each
  point plotted:

  void getPoint(void *object, gnuplot_point *point, int index, int pointCount);
    @param    obj     Pointer to an arbitrary object
    @param    point   Pointer to the returned point struct (double x, y, z)
    @param    i       Index of the current point: 0 to n-1
    @param    n       Number of points
    @return   void

  Example:

  Here `points` is an array of points:

  @code
    void PlotPoint(void *obj, gnuplot_point *point, int i, int n) {
      Point *p = (Point*) obj;
      point->x = p[i].x;
      point->y = p[i].y;
    }

    int main (int argc, char *argv[]) {
      ...
      gnuplot_plot_obj_xy(handle, points, PlotPoint, pCount, "Points");
      ...
    }
  @endcode

  Alternatively, `PlotPoint` could return values based on a complex formula and
  many sources of information. For example, it could be used to perform a
  Discrete Fourier Transform on an array of complex numbers, calculating one
  transformed point per call.

  Note: always explicitly set all relevant parts of the struct.
  However, the z component for 2D plots can be ignored.
 */
/*-------------------------------------------------------------------------*/

void gnuplot_plot_obj_xy (gnuplot_ctrl *handle, void *obj, void (*getPoint)(void *, gnuplot_point *, int, int), int n, char *title) {
  int tmpfd;
  char name[NAME_SIZE];
  char cmd[GP_CMD_SIZE];

  /* Error handling: mandatory arguments, already open session, opening temporary file */
  FAIL_IF (getPoint == NULL || (n < 1), "One of the parameters to gnuplot_plot_obj_xy() has been misspecified");
  gnuplot_i_error(handle);

  /* Open temporary file for output */
  sprintf(name, GNUPLOT_TEMPFILE, P_tmpdir);
  FAIL_IF ((tmpfd = mkstemp(name)) == -1, "Cannot create temporary file: exiting gnuplot_plot_obj_xy()");

  /* Store file name in array for future deletion */
  strcpy(handle->to_delete[handle->ntmp], name);
  handle->ntmp++;

  /* Write data to this file */
  gnuplot_point point;
  for (int i = 0; i < n; i++) {
    getPoint(obj, &point, i, n);
    sprintf(cmd, "%g %g\n", point.x, point.y);
    write(tmpfd, cmd, strlen(cmd));
  }
  close(tmpfd);

  /* Command to be sent to gnuplot */
  sprintf(cmd, "%s \"%s\" title \"%s\" with %s", (handle->nplots > 0) ? "replot" : "plot", name, (title) ? title : "No title", handle->pstyle);
  gnuplot_cmd(handle, cmd);
  handle->nplots++;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Open a new session, plot a signal, close the session.
  @param    title    Plot title
  @param    style    Plot style
  @param    label_x  Label for X
  @param    label_y  Label for Y
  @param    x        Array of X coordinates
  @param    y        Array of Y coordinates (can be NULL).
  @param    n        Number of values in x and y.
  @return   void

  This function opens a new gnuplot session, plots the provided signal as an X
  or XY signal depending on a provided y, waits for a carriage return on stdin
  and closes the session.

  An empty title, empty style, or empty labels for X and Y may be provided.
  Default values are provided in this case.
 */
/*--------------------------------------------------------------------------*/

void gnuplot_plot_once (char *style, char *label_x, char *label_y, double *x, double *y, int n, char *title) {
  /* Define handle as local variable to isolate it from other gnuplot sessions */
  gnuplot_ctrl *handle;

  /* Some error handling */
  FAIL_IF (x == NULL || n < 1, "One of the parameters to gnuplot_plot_once() has been misspecified");
  FAIL_IF ((handle = gnuplot_init()) == NULL, "Cannot initialize gnuplot handle");

  /* Generate commands to send to gnuplot */
  gnuplot_setstyle(handle, (style == NULL) ? "lines" : style);
  gnuplot_set_axislabel(handle, "x", (label_x == NULL) ? "X" : label_x);
  gnuplot_set_axislabel(handle, "y", (label_y == NULL) ? "Y" : label_y);
  gnuplot_plot_coordinates(handle, x, y, n, title);
  printf("Press Enter to continue\n");
  while (getchar() != '\n') {}
  gnuplot_close(handle);
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Plot a curve of given equation y=f(x).
  @param    handle        Gnuplot session control handle.
  @param    equation      Equation to plot.
  @param    title         Title of the plot (can be NULL).
  @return   void

  Plots a given equation. The general form of the equation is y=f(x),
  by providing the f(x) side of the equation only.

  Example:

  @code
    gnuplot_ctrl *h;
    char eq[80];

    h = gnuplot_init();
    strcpy(eq, "sin(x) * cos(2*x)");
    gnuplot_plot_equation(h, eq, "Oscillation", normal);
    gnuplot_close(h);
  @endcode
 */
/*--------------------------------------------------------------------------*/

void gnuplot_plot_equation (gnuplot_ctrl *handle, char *equation, char *title) {
  char cmd[GP_CMD_SIZE];

  sprintf(cmd, "%s %s title \"%s\" with %s", (handle->nplots > 0) ? "replot" : "plot", equation, (title) ? title : "No title", handle->pstyle);
  gnuplot_cmd(handle, cmd);
  handle->nplots++;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    Save a graph as a Postscript file on storage.
  @param    handle      Gnuplot session control handle.
  @param    filename    Filename to write to.
  @param    color       Any character to retain colors, no colors if NULL
  @return   void

  Sets the terminal to Postscript, replots the graph and then resets the
  terminal back to x11. This function supposes that it will be used in
  combination with one of the plotting functions, see example.

  Example:

  @code
    gnuplot_ctrl *h;
    char eq[80];

    h = gnuplot_init();
    strcpy(eq, "sin(x) * cos(2*x)");
    gnuplot_plot_equation(h, eq, "Oscillation", normal);
    gnuplot_hardcopy(h, "sinewave.ps", "color");
    gnuplot_close(h);
  @endcode
 */
/*--------------------------------------------------------------------------*/

void gnuplot_hardcopy (gnuplot_ctrl *handle, char *filename, char *color) {
  if (color == NULL) {
    gnuplot_cmd(handle, "set terminal postscript");
  } else {
    gnuplot_cmd(handle, "set terminal postscript enhanced color");
  }
  gnuplot_cmd(handle, "set output \"%s\"", filename);
  gnuplot_cmd(handle, "replot");
  gnuplot_cmd(handle, "set terminal %s", handle->term);
}

#endif

/* vim: set ts=4 et sw=4 tw=75 */
