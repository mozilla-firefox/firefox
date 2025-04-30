PR_IMPLEMENT
============

Used to define implementations of symbols that are to be exported from a
shared library.


Syntax
------

.. code::

   #include <prtypes.h>

   PR_IMPLEMENT(type)implementation


Description
-----------

:ref:`PR_IMPLEMENT` is used to define implementations of externally visible
routines and globals. For syntax details for each platform, see
`prtypes.h <https://searchfox.org/mozilla-central/source/nsprpub/pr/include/prtypes.h>`__.

.. warning::

   **Warning**: Some platforms do not allow the use of the underscore
   character (_) as the first character of an exported symbol.
