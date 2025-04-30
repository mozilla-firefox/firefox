PR_EXTERN
=========

Used to define the prototypes for functions or variables that are to be
exported from a shared library.


Syntax
------

.. code::

   #include <prtypes.h>

   PR_EXTERN(type)prototype


Description
-----------

:ref:`PR_EXTERN` is used to define externally visible routines and globals.
For syntax details for each platform, see
`prtypes.h <https://searchfox.org/mozilla-central/source/nsprpub/pr/include/prtypes.h>`__.
The macro includes the proper specifications to declare the target
``extern`` and set up other required linkages.

.. warning::

   **Warning**: Some platforms do not allow the use of the underscore
   character (_) as the first character of an exported symbol.
