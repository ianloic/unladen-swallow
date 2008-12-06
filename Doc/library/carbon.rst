
.. _toolbox:

**********************
Mac OS Toolbox Modules
**********************

There are a set of modules that provide interfaces to various Mac OS toolboxes.
If applicable the module will define a number of Python objects for the various
structures declared by the toolbox, and operations will be implemented as
methods of the object.  Other operations will be implemented as functions in the
module.  Not all operations possible in C will also be possible in Python
(callbacks are often a problem), and parameters will occasionally be different
in Python (input and output buffers, especially).  All methods and functions
have a :attr:`__doc__` string describing their arguments and return values, and
for additional description you are referred to `Inside Macintosh
<http://developer.apple.com/documentation/macos8/mac8.html>`_ or similar works.

These modules all live in a package called :mod:`Carbon`. Despite that name they
are not all part of the Carbon framework: CF is really in the CoreFoundation
framework and Qt is in the QuickTime framework. The normal use pattern is ::

   from Carbon import AE

.. warning::

   The Carbon modules are removed in 3.0.


:mod:`Carbon.AE` --- Apple Events
=================================

.. module:: Carbon.AE
   :platform: Mac
   :synopsis: Interface to the Apple Events toolbox.
   :deprecated:



:mod:`Carbon.AH` --- Apple Help
===============================

.. module:: Carbon.AH
   :platform: Mac
   :synopsis: Interface to the Apple Help manager.
   :deprecated:



:mod:`Carbon.App` --- Appearance Manager
========================================

.. module:: Carbon.App
   :platform: Mac
   :synopsis: Interface to the Appearance Manager.
   :deprecated:



:mod:`Carbon.CF` --- Core Foundation
====================================

.. module:: Carbon.CF
   :platform: Mac
   :synopsis: Interface to the Core Foundation.
   :deprecated:


The ``CFBase``, ``CFArray``, ``CFData``, ``CFDictionary``, ``CFString`` and
``CFURL`` objects are supported, some only partially.


:mod:`Carbon.CG` --- Core Graphics
==================================

.. module:: Carbon.CG
   :platform: Mac
   :synopsis: Interface to Core Graphics.
   :deprecated:



:mod:`Carbon.CarbonEvt` --- Carbon Event Manager
================================================

.. module:: Carbon.CarbonEvt
   :platform: Mac
   :synopsis: Interface to the Carbon Event Manager.
   :deprecated:



:mod:`Carbon.Cm` --- Component Manager
======================================

.. module:: Carbon.Cm
   :platform: Mac
   :synopsis: Interface to the Component Manager.
   :deprecated:



:mod:`Carbon.Ctl` --- Control Manager
=====================================

.. module:: Carbon.Ctl
   :platform: Mac
   :synopsis: Interface to the Control Manager.
   :deprecated:



:mod:`Carbon.Dlg` --- Dialog Manager
====================================

.. module:: Carbon.Dlg
   :platform: Mac
   :synopsis: Interface to the Dialog Manager.
   :deprecated:



:mod:`Carbon.Evt` --- Event Manager
===================================

.. module:: Carbon.Evt
   :platform: Mac
   :synopsis: Interface to the classic Event Manager.
   :deprecated:



:mod:`Carbon.Fm` --- Font Manager
=================================

.. module:: Carbon.Fm
   :platform: Mac
   :synopsis: Interface to the Font Manager.
   :deprecated:



:mod:`Carbon.Folder` --- Folder Manager
=======================================

.. module:: Carbon.Folder
   :platform: Mac
   :synopsis: Interface to the Folder Manager.
   :deprecated:



:mod:`Carbon.Help` --- Help Manager
===================================

.. module:: Carbon.Help
   :platform: Mac
   :synopsis: Interface to the Carbon Help Manager.
   :deprecated:



:mod:`Carbon.List` --- List Manager
===================================

.. module:: Carbon.List
   :platform: Mac
   :synopsis: Interface to the List Manager.
   :deprecated:



:mod:`Carbon.Menu` --- Menu Manager
===================================

.. module:: Carbon.Menu
   :platform: Mac
   :synopsis: Interface to the Menu Manager.
   :deprecated:



:mod:`Carbon.Mlte` --- MultiLingual Text Editor
===============================================

.. module:: Carbon.Mlte
   :platform: Mac
   :synopsis: Interface to the MultiLingual Text Editor.
   :deprecated:



:mod:`Carbon.Qd` --- QuickDraw
==============================

.. module:: Carbon.Qd
   :platform: Mac
   :synopsis: Interface to the QuickDraw toolbox.
   :deprecated:



:mod:`Carbon.Qdoffs` --- QuickDraw Offscreen
============================================

.. module:: Carbon.Qdoffs
   :platform: Mac
   :synopsis: Interface to the QuickDraw Offscreen APIs.
   :deprecated:



:mod:`Carbon.Qt` --- QuickTime
==============================

.. module:: Carbon.Qt
   :platform: Mac
   :synopsis: Interface to the QuickTime toolbox.
   :deprecated:



:mod:`Carbon.Res` --- Resource Manager and Handles
==================================================

.. module:: Carbon.Res
   :platform: Mac
   :synopsis: Interface to the Resource Manager and Handles.
   :deprecated:



:mod:`Carbon.Scrap` --- Scrap Manager
=====================================

.. module:: Carbon.Scrap
   :platform: Mac
   :synopsis: The Scrap Manager provides basic services for implementing cut & paste and
              clipboard operations.
   :deprecated:


This module is only fully available on Mac OS 9 and earlier under classic PPC
MacPython.  Very limited functionality is available under Carbon MacPython.

.. index:: single: Scrap Manager

The Scrap Manager supports the simplest form of cut & paste operations on the
Macintosh.  It can be use for both inter- and intra-application clipboard
operations.

The :mod:`Scrap` module provides low-level access to the functions of the Scrap
Manager.  It contains the following functions:


.. function:: InfoScrap()

   Return current information about the scrap.  The information is encoded as a
   tuple containing the fields ``(size, handle, count, state, path)``.

   +----------+---------------------------------------------+
   | Field    | Meaning                                     |
   +==========+=============================================+
   | *size*   | Size of the scrap in bytes.                 |
   +----------+---------------------------------------------+
   | *handle* | Resource object representing the scrap.     |
   +----------+---------------------------------------------+
   | *count*  | Serial number of the scrap contents.        |
   +----------+---------------------------------------------+
   | *state*  | Integer; positive if in memory, ``0`` if on |
   |          | disk, negative if uninitialized.            |
   +----------+---------------------------------------------+
   | *path*   | Filename of the scrap when stored on disk.  |
   +----------+---------------------------------------------+


.. seealso::

   `Scrap Manager <http://developer.apple.com/documentation/mac/MoreToolbox/MoreToolbox-109.html>`_
      Apple's documentation for the Scrap Manager gives a lot of useful information
      about using the Scrap Manager in applications.



:mod:`Carbon.Snd` --- Sound Manager
===================================

.. module:: Carbon.Snd
   :platform: Mac
   :synopsis: Interface to the Sound Manager.
   :deprecated:



:mod:`Carbon.TE` --- TextEdit
=============================

.. module:: Carbon.TE
   :platform: Mac
   :synopsis: Interface to TextEdit.
   :deprecated:



:mod:`Carbon.Win` --- Window Manager
====================================

.. module:: Carbon.Win
   :platform: Mac
   :synopsis: Interface to the Window Manager.
   :deprecated:
