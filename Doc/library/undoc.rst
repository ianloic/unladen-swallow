
.. _undoc:

********************
Undocumented Modules
********************

Here's a quick listing of modules that are currently undocumented, but that
should be documented.  Feel free to contribute documentation for them!  (Send
via email to docs@python.org.)

The idea and original contents for this chapter were taken from a posting by
Fredrik Lundh; the specific contents of this chapter have been substantially
revised.


Miscellaneous useful utilities
==============================

Some of these are very old and/or not very robust; marked with "hmm."

:mod:`ihooks`
   --- Import hook support (for :mod:`rexec`; may become obsolete).
   
   .. warning:: The :mod:`ihooks` module has been removed in Python 3.0.


Platform specific modules
=========================

These modules are used to implement the :mod:`os.path` module, and are not
documented beyond this mention.  There's little need to document these.

:mod:`ntpath`
   --- Implementation of :mod:`os.path` on Win32, Win64, WinCE, and OS/2 platforms.

:mod:`posixpath`
   --- Implementation of :mod:`os.path` on POSIX.

:mod:`bsddb185`
   --- Backwards compatibility module for systems which still use the Berkeley DB
   1.85 module.  It is normally only available on certain BSD Unix-based systems.
   It should never be used directly.


Multimedia
==========

:mod:`audiodev`
   --- Platform-independent API for playing audio data.

   .. warning:: The :mod:`audiodev` module has been removed in 3.0.

:mod:`linuxaudiodev`
   --- Play audio data on the Linux audio device.  Replaced in Python 2.3 by the
   :mod:`ossaudiodev` module.
   
   .. warning:: The :mod:`linuxaudiodev` module has been removed in Python 3.0.

:mod:`sunaudio`
   --- Interpret Sun audio headers (may become obsolete or a tool/demo).

   .. warning:: The :mod:`sunaudio` module has been removed in Python 3.0.

:mod:`toaiff`
   --- Convert "arbitrary" sound files to AIFF files; should probably become a tool
   or demo.  Requires the external program :program:`sox`.


   .. warning:: The :mod:`toaiff` module has been removed in 3.0.


.. _undoc-mac-modules:

Undocumented Mac OS modules
===========================


:mod:`applesingle` --- AppleSingle decoder
------------------------------------------

.. module:: applesingle
   :platform: Mac
   :synopsis: Rudimentary decoder for AppleSingle format files.
   :deprecated:

.. deprecated:: 2.6


:mod:`buildtools` --- Helper module for BuildApplet and Friends
---------------------------------------------------------------

.. module:: buildtools
   :platform: Mac
   :synopsis: Helper module for BuildApplet, BuildApplication and macfreeze.
   :deprecated:


.. deprecated:: 2.4

:mod:`cfmfile` --- Code Fragment Resource module
------------------------------------------------

.. module:: cfmfile
   :platform: Mac
   :synopsis: Code Fragment Resource module.
   :deprecated:


:mod:`cfmfile` is a module that understands Code Fragments and the accompanying
"cfrg" resources. It can parse them and merge them, and is used by
BuildApplication to combine all plugin modules to a single executable.

.. deprecated:: 2.4

:mod:`icopen` --- Internet Config replacement for :meth:`open`
--------------------------------------------------------------

.. module:: icopen
   :platform: Mac
   :synopsis: Internet Config replacement for open().
   :deprecated:


Importing :mod:`icopen` will replace the builtin :meth:`open` with a version
that uses Internet Config to set file type and creator for new files.

.. deprecated:: 2.6


:mod:`macerrors` --- Mac OS Errors
----------------------------------

.. module:: macerrors
   :platform: Mac
   :synopsis: Constant definitions for many Mac OS error codes.
   :deprecated:


:mod:`macerrors` contains constant definitions for many Mac OS error codes.

.. deprecated:: 2.6


:mod:`macresource` --- Locate script resources
----------------------------------------------

.. module:: macresource
   :platform: Mac
   :synopsis: Locate script resources.
   :deprecated:


:mod:`macresource` helps scripts finding their resources, such as dialogs and
menus, without requiring special case code for when the script is run under
MacPython, as a MacPython applet or under OSX Python.

.. deprecated:: 2.6


:mod:`Nav` --- NavServices calls
--------------------------------

.. module:: Nav
   :platform: Mac
   :synopsis: Interface to Navigation Services.
   :deprecated:


A low-level interface to Navigation Services.

.. deprecated:: 2.6


:mod:`PixMapWrapper` --- Wrapper for PixMap objects
---------------------------------------------------

.. module:: PixMapWrapper
   :platform: Mac
   :synopsis: Wrapper for PixMap objects.
   :deprecated:


:mod:`PixMapWrapper` wraps a PixMap object with a Python object that allows
access to the fields by name. It also has methods to convert to and from
:mod:`PIL` images.

.. deprecated:: 2.6


:mod:`videoreader` --- Read QuickTime movies
--------------------------------------------

.. module:: videoreader
   :platform: Mac
   :synopsis: Read QuickTime movies frame by frame for further processing.
   :deprecated:


:mod:`videoreader` reads and decodes QuickTime movies and passes a stream of
images to your program. It also provides some support for audio tracks.

.. deprecated:: 2.6


:mod:`W` --- Widgets built on :mod:`FrameWork`
----------------------------------------------

.. module:: W
   :platform: Mac
   :synopsis: Widgets for the Mac, built on top of FrameWork.
   :deprecated:


The :mod:`W` widgets are used extensively in the :program:`IDE`.

.. deprecated:: 2.6


.. _obsolete-modules:

Obsolete
========

These modules are not normally available for import; additional work must be
done to make them available.

These extension modules written in C are not built by default. Under Unix, these
must be enabled by uncommenting the appropriate lines in :file:`Modules/Setup`
in the build tree and either rebuilding Python if the modules are statically
linked, or building and installing the shared object if using dynamically-loaded
extensions.

.. (lib-old is empty as of Python 2.5)

   Those which are written in Python will be installed into the directory
   \file{lib-old/} installed as part of the standard library.  To use
   these, the directory must be added to \code{sys.path}, possibly using
   \envvar{PYTHONPATH}.

:mod:`timing`
   --- Measure time intervals to high resolution (use :func:`time.clock` instead).
   
   .. warning:: The :mod:`timing` module has been removed in Python 3.0.


SGI-specific Extension modules
==============================

The following are SGI specific, and may be out of touch with the current version
of reality.

:mod:`cl`
   --- Interface to the SGI compression library.

:mod:`sv`
   --- Interface to the "simple video" board on SGI Indigo (obsolete hardware).
   
   .. warning:: The :mod:`sv` module has been removed in Python 3.0.

