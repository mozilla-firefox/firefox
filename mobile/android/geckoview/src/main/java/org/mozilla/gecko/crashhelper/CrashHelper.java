/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.crashhelper;

import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Binder;
import android.os.DeadObjectException;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.os.RemoteException;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.util.Log;
import java.io.FileDescriptor;
import java.io.IOException;
import org.mozilla.gecko.mozglue.GeckoLoader;
import org.mozilla.geckoview.BuildConfig;

public final class CrashHelper extends Service {
  private static final String LOGTAG = "GeckoCrashHelper";
  private static final boolean DEBUG = !BuildConfig.MOZILLA_OFFICIAL;

  private static boolean sNativeLibLoaded;

  private final Binder mBinder = new CrashHelperBinder();

  @Override
  public synchronized void onCreate() {
    if (!sNativeLibLoaded) {
      GeckoLoader.doLoadLibrary(this, "crashhelper");
      sNativeLibLoaded = true;
    }
  }

  private static class CrashHelperBinder extends ICrashHelper.Stub {
    @Override
    public boolean start(
        final int clientPid,
        final ParcelFileDescriptor breakpadFd,
        final String minidumpPath,
        final ParcelFileDescriptor listenFd,
        final ParcelFileDescriptor serverFd) {
      // Launch the crash helper code, this will spin out a thread which will
      // handle the IPC with Firefox' other processes. When running junit tests
      // we might have several main processes connecting to the service. Each
      // will get its own thread, but the generated crashes live in a shared
      // space. This means that technically one main process instance might
      // "steal" the crash report of another main process. We should add
      // additional separation within the crash generation code to prevent this
      // from happening even though it's very unlikely.
      CrashHelper.crash_generator(
          clientPid, breakpadFd.detachFd(), minidumpPath, listenFd.detachFd(), serverFd.detachFd());

      return false;
    }
  }

  @Override
  public IBinder onBind(final Intent intent) {
    return mBinder;
  }

  public static ServiceConnection createConnection(
      final ParcelFileDescriptor breakpadFd,
      final String minidumpPath,
      final ParcelFileDescriptor listenFd,
      final ParcelFileDescriptor serverFd) {
    class CrashHelperConnection implements ServiceConnection {
      public CrashHelperConnection(
          final ParcelFileDescriptor breakpadFd,
          final String minidumpPath,
          final ParcelFileDescriptor listenFd,
          final ParcelFileDescriptor serverFd) {
        mBreakpadFd = breakpadFd;
        mMinidumpPath = minidumpPath;
        mListenFd = listenFd;
        mServerFd = serverFd;
      }

      @Override
      public void onServiceConnected(final ComponentName name, final IBinder service) {
        final ICrashHelper helper = ICrashHelper.Stub.asInterface(service);
        try {
          helper.start(Process.myPid(), mBreakpadFd, mMinidumpPath, mListenFd, mServerFd);
        } catch (final DeadObjectException e) {
          // The crash helper process died before we could start it, presumably
          // because of an out-of-memory condition. We don't attempt to restart
          // it as the required IPC would be too complex.
          Log.e(LOGTAG, "The crash helper process died before we could start the service");
        } catch (final RemoteException e) {
          throw new RuntimeException(e);
        }
      }

      @Override
      public void onServiceDisconnected(final ComponentName name) {
        // Nothing to do here
      }

      ParcelFileDescriptor mBreakpadFd;
      String mMinidumpPath;
      ParcelFileDescriptor mListenFd;
      ParcelFileDescriptor mServerFd;
    }

    return new CrashHelperConnection(breakpadFd, minidumpPath, listenFd, serverFd);
  }

  public static final class Pipes {
    public final ParcelFileDescriptor mBreakpadClient;
    public final ParcelFileDescriptor mBreakpadServer;
    public final ParcelFileDescriptor mListener;
    public final ParcelFileDescriptor mClient;
    public final ParcelFileDescriptor mServer;

    public Pipes(
        final FileDescriptor breakpadClientFd,
        final FileDescriptor breakpadServerFd,
        final FileDescriptor listenerFd,
        final FileDescriptor clientFd,
        final FileDescriptor serverFd)
        throws IOException {
      mBreakpadClient = ParcelFileDescriptor.dup(breakpadClientFd);
      mBreakpadServer = ParcelFileDescriptor.dup(breakpadServerFd);
      if (!CrashHelper.set_breakpad_opts(mBreakpadServer.getFd())) {
        throw new IOException("Could not set the proper options on the Breakpad socket");
      }
      mListener = ParcelFileDescriptor.dup(listenerFd);
      if (!CrashHelper.bind_and_listen(mListener.getFd())) {
        throw new IOException("Could not listen on incoming connections");
      }
      mClient = ParcelFileDescriptor.dup(clientFd);
      mServer = ParcelFileDescriptor.dup(serverFd);
    }
  }

  // This builds five sockets used for communication between Gecko and the
  // crash helper process. The first two are a socket pair identical to the one
  // created via a call to
  // google_breakpad::CrashGenerationServer::CreateReportChannel(), so we can
  // use them Breakpad's crash generation server & clients. The rest are
  // specific to the crash helper process.
  public static Pipes createCrashHelperPipes(final Context context) {
    try {
      // We can't set the required socket options for the Breakpad server socket
      // or our own listener from here, so we delegate those parts to native
      // functions in crashhelper_android.cpp.
      GeckoLoader.doLoadLibrary(null, "crashhelper");

      final FileDescriptor breakpad_client_fd = new FileDescriptor();
      final FileDescriptor breakpad_server_fd = new FileDescriptor();
      Os.socketpair(
          OsConstants.AF_UNIX,
          OsConstants.SOCK_SEQPACKET,
          0,
          breakpad_client_fd,
          breakpad_server_fd);
      final FileDescriptor listener_fd =
          Os.socket(OsConstants.AF_UNIX, OsConstants.SOCK_SEQPACKET, 0);
      final FileDescriptor client_fd = new FileDescriptor();
      final FileDescriptor server_fd = new FileDescriptor();
      Os.socketpair(OsConstants.AF_UNIX, OsConstants.SOCK_SEQPACKET, 0, client_fd, server_fd);

      final Pipes pipes =
          new Pipes(breakpad_client_fd, breakpad_server_fd, listener_fd, client_fd, server_fd);

      // Manually close all the file descriptors we created.
      // ParcelFileDescriptor instances in the Pipes object will close their
      // underlying fds when garbage-collected but FileDescriptor instances do
      // not, leaving us the job to clean them up.
      Os.close(breakpad_client_fd);
      Os.close(breakpad_server_fd);
      Os.close(listener_fd);
      Os.close(client_fd);
      Os.close(server_fd);
      return pipes;
    } catch (final ErrnoException | IOException | RuntimeException e) {
      Log.e(LOGTAG, "Could not create the crash helper pipes: " + e.toString());
      return null;
    }
  }

  // Note: we don't implement onDestroy() because the service has the
  // `stopWithTask` flag set in the manifest, so the service manager will
  // tear it down for us.

  protected static native void crash_generator(
      int clientPid, int breakpadFd, String minidumpPath, int listenFd, int serverFd);

  protected static native boolean set_breakpad_opts(int breakpadFd);

  protected static native boolean bind_and_listen(int listenFd);
}
