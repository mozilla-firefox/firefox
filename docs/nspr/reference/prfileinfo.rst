PRFileInfo
==========

File information structure used with :ref:`PR_GetFileInfo` and
:ref:`PR_GetOpenFileInfo`.


Syntax
~~~~~~

.. code::

   #include <prio.h>

   struct PRFileInfo {
      PRFileType type;
      PRUint32 size;
      PRTime creationTime;
      PRTime modifyTime;
   };

   typedef struct PRFileInfo PRFileInfo;




Fields
~~~~~~

The structure has the following fields:

``type``
   Type of file. See :ref:`PRFileType`.
``size``
   Size, in bytes, of file's contents.
``creationTime``
   Creation time per definition of :ref:`PRTime`. See
   `prtime.h <https://searchfox.org/mozilla-central/source/nsprpub/pr/include/prtime.h>`__.
``modifyTime``
   Last modification time per definition of :ref:`PRTime`. See
   `prtime.h <https://searchfox.org/mozilla-central/source/nsprpub/pr/include/prtime.h>`__.


Description
-----------

The :ref:`PRFileInfo` structure provides information about a file, a
directory, or some other kind of file system object, as specified by the
``type`` field.
