/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/CondVar.h"
#include "mozilla/Monitor.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/Mutex.h"
#include "gtest/gtest.h"

using namespace mozilla;

static PRThread* spawn_thread(void (*run)(void*), void* arg) {
  return PR_CreateThread(PR_SYSTEM_THREAD, run, arg, PR_PRIORITY_NORMAL,
                         PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
}

//-----------------------------------------------------------------------------
// Sanity check: tests that can be done on a single thread
//
TEST(Synchronization, Sanity)
MOZ_NO_THREAD_SAFETY_ANALYSIS {
  Mutex lock("sanity::lock");
  lock.Lock();
  lock.AssertCurrentThreadOwns();
  lock.Unlock();

  {
    MutexAutoLock autolock(lock);
    lock.AssertCurrentThreadOwns();
  }

  lock.Lock();
  lock.AssertCurrentThreadOwns();
  {
    MutexAutoUnlock autounlock(lock);
  }
  lock.AssertCurrentThreadOwns();
  lock.Unlock();

  ReentrantMonitor mon("sanity::monitor");
  mon.Enter();
  mon.AssertCurrentThreadIn();
  mon.Enter();
  mon.AssertCurrentThreadIn();
  mon.Exit();
  mon.AssertCurrentThreadIn();
  mon.Exit();

  {
    ReentrantMonitorAutoEnter automon(mon);
    mon.AssertCurrentThreadIn();
  }
}

//-----------------------------------------------------------------------------
// Mutex contention tests
//
static Mutex* gLock1;

static void MutexContention_thread(void* /*arg*/) {
  for (int i = 0; i < 100000; ++i) {
    gLock1->Lock();
    gLock1->AssertCurrentThreadOwns();
    gLock1->Unlock();
  }
}

TEST(Synchronization, MutexContention)
{
  gLock1 = new Mutex("lock1");
  // PURPOSELY not checking for OOM.  YAY!

  PRThread* t1 = spawn_thread(MutexContention_thread, nullptr);
  PRThread* t2 = spawn_thread(MutexContention_thread, nullptr);
  PRThread* t3 = spawn_thread(MutexContention_thread, nullptr);

  PR_JoinThread(t1);
  PR_JoinThread(t2);
  PR_JoinThread(t3);

  delete gLock1;
}

//-----------------------------------------------------------------------------
// Monitor tests
//
static Monitor* gMon1;

static void MonitorContention_thread(void* /*arg*/) {
  for (int i = 0; i < 100000; ++i) {
    gMon1->Lock();
    gMon1->AssertCurrentThreadOwns();
    gMon1->Unlock();
  }
}

TEST(Synchronization, MonitorContention)
{
  gMon1 = new Monitor("mon1");

  PRThread* t1 = spawn_thread(MonitorContention_thread, nullptr);
  PRThread* t2 = spawn_thread(MonitorContention_thread, nullptr);
  PRThread* t3 = spawn_thread(MonitorContention_thread, nullptr);

  PR_JoinThread(t1);
  PR_JoinThread(t2);
  PR_JoinThread(t3);

  delete gMon1;
}

static ReentrantMonitor* gMon2;

static void MonitorContention2_thread(void* /*arg*/)
    MOZ_NO_THREAD_SAFETY_ANALYSIS {
  for (int i = 0; i < 100000; ++i) {
    gMon2->Enter();
    gMon2->AssertCurrentThreadIn();
    {
      gMon2->Enter();
      gMon2->AssertCurrentThreadIn();
      gMon2->Exit();
    }
    gMon2->AssertCurrentThreadIn();
    gMon2->Exit();
  }
}

TEST(Synchronization, MonitorContention2)
{
  gMon2 = new ReentrantMonitor("mon1");

  PRThread* t1 = spawn_thread(MonitorContention2_thread, nullptr);
  PRThread* t2 = spawn_thread(MonitorContention2_thread, nullptr);
  PRThread* t3 = spawn_thread(MonitorContention2_thread, nullptr);

  PR_JoinThread(t1);
  PR_JoinThread(t2);
  PR_JoinThread(t3);

  delete gMon2;
}

static ReentrantMonitor* gMon3;
static int32_t gMonFirst;

static void MonitorSyncSanity_thread(void* /*arg*/)
    MOZ_NO_THREAD_SAFETY_ANALYSIS {
  gMon3->Enter();
  gMon3->AssertCurrentThreadIn();
  if (gMonFirst) {
    gMonFirst = 0;
    gMon3->Wait();
    gMon3->Enter();
  } else {
    gMon3->Notify();
    gMon3->Enter();
  }
  gMon3->AssertCurrentThreadIn();
  gMon3->Exit();
  gMon3->AssertCurrentThreadIn();
  gMon3->Exit();
}

TEST(Synchronization, MonitorSyncSanity)
{
  gMon3 = new ReentrantMonitor("monitor::syncsanity");

  for (int32_t i = 0; i < 10000; ++i) {
    gMonFirst = 1;
    PRThread* ping = spawn_thread(MonitorSyncSanity_thread, nullptr);
    PRThread* pong = spawn_thread(MonitorSyncSanity_thread, nullptr);
    PR_JoinThread(ping);
    PR_JoinThread(pong);
  }

  delete gMon3;
}

//-----------------------------------------------------------------------------
// Condvar tests
//
static Mutex* gCvlock1;
static CondVar* gCv1;
static int32_t gCvFirst;

static void CondVarSanity_thread(void* /*arg*/) {
  gCvlock1->Lock();
  gCvlock1->AssertCurrentThreadOwns();
  if (gCvFirst) {
    gCvFirst = 0;
    gCv1->Wait();
  } else {
    gCv1->Notify();
  }
  gCvlock1->AssertCurrentThreadOwns();
  gCvlock1->Unlock();
}

TEST(Synchronization, CondVarSanity)
{
  gCvlock1 = new Mutex("cvlock1");
  gCv1 = new CondVar(*gCvlock1, "cvlock1");

  for (int32_t i = 0; i < 10000; ++i) {
    gCvFirst = 1;
    PRThread* ping = spawn_thread(CondVarSanity_thread, nullptr);
    PRThread* pong = spawn_thread(CondVarSanity_thread, nullptr);
    PR_JoinThread(ping);
    PR_JoinThread(pong);
  }

  delete gCv1;
  delete gCvlock1;
}

//-----------------------------------------------------------------------------
// AutoLock tests
//
TEST(Synchronization, AutoLock)
{
  Mutex l1 MOZ_UNANNOTATED("autolock");
  MutexAutoLock autol1(l1);

  l1.AssertCurrentThreadOwns();

  {
    Mutex l2 MOZ_UNANNOTATED("autolock2");
    MutexAutoLock autol2(l2);

    l1.AssertCurrentThreadOwns();
    l2.AssertCurrentThreadOwns();
  }

  l1.AssertCurrentThreadOwns();
}

//-----------------------------------------------------------------------------
// AutoTryLock tests
//
// The thread owns assertions make mutex analysis throw spurious warnings
TEST(Synchronization, AutoTryLock)
MOZ_NO_THREAD_SAFETY_ANALYSIS {
  Mutex l1 MOZ_UNANNOTATED("autotrylock");
  MutexAutoTryLock autol1(l1);

  EXPECT_TRUE(autol1);
  l1.AssertCurrentThreadOwns();

  MutexAutoTryLock autol2(l1);

  EXPECT_TRUE(autol1);
  EXPECT_FALSE(autol2);
  l1.AssertCurrentThreadOwns();

  {
    Mutex l2 MOZ_UNANNOTATED("autotrylock2");
    MutexAutoTryLock autol3(l2);

    EXPECT_TRUE(autol3);
    l1.AssertCurrentThreadOwns();
    l2.AssertCurrentThreadOwns();
  }

  l1.AssertCurrentThreadOwns();
}

//-----------------------------------------------------------------------------
// AutoUnlock tests
//
TEST(Synchronization, AutoUnlock)
{
  Mutex l1 MOZ_UNANNOTATED("autounlock");
  Mutex l2 MOZ_UNANNOTATED("autounlock2");

  l1.Lock();
  l1.AssertCurrentThreadOwns();

  {
    MutexAutoUnlock autol1(l1);
    {
      l2.Lock();
      l2.AssertCurrentThreadOwns();

      MutexAutoUnlock autol2(l2);
    }
    l2.AssertCurrentThreadOwns();
    l2.Unlock();
  }
  l1.AssertCurrentThreadOwns();

  l1.Unlock();
}

//-----------------------------------------------------------------------------
// AutoMonitor tests
//
TEST(Synchronization, AutoMonitor)
MOZ_NO_THREAD_SAFETY_ANALYSIS {
  ReentrantMonitor m1("automonitor");
  ReentrantMonitor m2("automonitor2");

  m1.Enter();
  m1.AssertCurrentThreadIn();
  {
    ReentrantMonitorAutoEnter autom1(m1);
    m1.AssertCurrentThreadIn();

    m2.Enter();
    m2.AssertCurrentThreadIn();
    {
      ReentrantMonitorAutoEnter autom2(m2);
      m1.AssertCurrentThreadIn();
      m2.AssertCurrentThreadIn();
    }
    m2.AssertCurrentThreadIn();
    m2.Exit();

    m1.AssertCurrentThreadIn();
  }
  m1.AssertCurrentThreadIn();
  m1.Exit();
}
