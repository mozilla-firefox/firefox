/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsUserIdleService_h_
#define nsUserIdleService_h_

#include "mozilla/TimeStamp.h"
#include "nsCOMPtr.h"
#include "nsCategoryCache.h"
#include "nsIObserver.h"
#include "nsITimer.h"
#include "nsIUserIdleService.h"
#include "nsIUserIdleServiceInternal.h"
#include "nsTArray.h"
#include "nsWeakReference.h"

/**
 * Class we can use to store an observer with its associated idle time
 * requirement and whether or not the observer thinks it's "idle".
 */
class IdleListener {
 public:
  nsCOMPtr<nsIObserver> observer;
  uint32_t reqIdleTime;
  bool isIdle;

  IdleListener(nsIObserver* obs, uint32_t reqIT, bool aIsIdle = false)
      : observer(obs), reqIdleTime(reqIT), isIdle(aIsIdle) {}
  ~IdleListener() = default;
};

// This one will be declared later.
class nsUserIdleService;

/**
 * Class to handle the daily idle timer.
 */
class nsUserIdleServiceDaily : public nsIObserver,
                               public nsSupportsWeakReference {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  explicit nsUserIdleServiceDaily(nsIUserIdleService* aIdleService);

  /**
   * Initializes the daily idle observer.
   * Keep this separated from the constructor, since it could cause pointer
   * corruption due to AddRef/Release of "this".
   */
  void Init();

 private:
  virtual ~nsUserIdleServiceDaily();

  /**
   * StageIdleDaily is the interim call made when an idle-daily event is due.
   * However we don't want to fire idle-daily until the user is idle for this
   * session, so this sets up a short wait for an idle event which triggers
   * the actual idle-daily event.
   *
   * @param aHasBeenLongWait Pass true indicating nsUserIdleServiceDaily is
   * having trouble getting the idle-daily event fired. If true StageIdleDaily
   * will use a shorter idle wait time before firing idle-daily.
   */
  void StageIdleDaily(bool aHasBeenLongWait);

  /**
   * @note This is a normal pointer, part to avoid creating a cycle with the
   * idle service, part to avoid potential pointer corruption due to this class
   * being instantiated in the constructor of the service itself.
   */
  nsIUserIdleService* mIdleService;

  /**
   * Place to hold the timer used by this class to determine when a day has
   * passed, after that it will wait for idle time to be detected.
   */
  nsCOMPtr<nsITimer> mTimer;

  /**
   * Function that is called back once a day.
   */
  static void DailyCallback(nsITimer* aTimer, void* aClosure);

  /**
   * Cache of observers for the "idle-daily" category.
   */
  nsCategoryCache<nsIObserver> mCategoryObservers;

  /**
   * Next time we expect an idle-daily timer to fire, in case timers aren't
   * very reliable on the platform. Value is in PR_Now microsecond units.
   */
  PRTime mExpectedTriggerTime;

  /**
   * Tracks which idle daily observer callback we ask for. There are two: a
   * regular long idle wait and a shorter wait if we've been waiting to fire
   * idle daily for an extended period. Set by StageIdleDaily.
   */
  int32_t mIdleDailyTriggerWait;
};

class nsUserIdleService : public nsIUserIdleServiceInternal {
 public:
  NS_DECL_ISUPPORTS
 NS_DECL_NSIUSERIDLESERVICE NS_DECL_NSIUSERIDLESERVICEINTERNAL

     protected : static already_AddRefed<nsUserIdleService>
                 GetInstance();

  nsUserIdleService();
  virtual ~nsUserIdleService();

  /**
   * If there is a platform specific function to poll the system idel time
   * then that must be returned in this function, and the function MUST return
   * true, otherwise then the function should be left unimplemented or made
   * to return false (this can also be used for systems where it depends on
   * the configuration of the system if the idle time can be determined)
   *
   * @param aIdleTime
   *        The idle time in ms.
   *
   * @return true if the idle time could be polled, false otherwise.
   *
   * @note The time returned by this function can be different than the one
   *       returned by GetIdleTime, as that is corrected by any calls to
   *       ResetIdleTimeOut(), unless you overwrite that function too...
   */
  virtual bool PollIdleTime(uint32_t* aIdleTime);

 public:
  void SetDisabledForShutdown();

 private:
  /**
   * Ensure that the timer is expiring at least at the given time
   *
   * The function might not restart the timer if there is one running currently
   *
   * @param aNextTimeout
   *        The last absolute time the timer should expire
   */
  void SetTimerExpiryIfBefore(mozilla::TimeStamp aNextTimeout);

  /**
   * Stores the next timeout time, 0 means timer not running
   */
  mozilla::TimeStamp mCurrentlySetToTimeoutAt;

  /**
   * mTimer holds the internal timer used by this class to detect when to poll
   * for idle time, when to check if idle timers should expire etc.
   */
  nsCOMPtr<nsITimer> mTimer;

  /**
   * Array of listeners that wants to be notified about idle time.
   */
  nsTArray<IdleListener> mArrayListeners;

  /**
   * Object keeping track of the daily idle thingy.
   */
  RefPtr<nsUserIdleServiceDaily> mDailyIdle;

  /**
   * Number of observers currently in idle mode.
   */
  uint32_t mIdleObserverCount;

  /**
   * Delta time from last non idle time to when the next observer should switch
   * to idle mode
   *
   * Time in seconds
   *
   * If this value is 0 it means there are no active observers
   */
  uint32_t mDeltaToNextIdleSwitchInS;

  /**
   * If true, the idle service is temporarily disabled, and all idle events
   * will be ignored.
   */
  bool mDisabled = false;

  /**
   * Set at AppShutdownConfirmed: observers have been dropped, and adding or
   * removing one does nothing anymore.
   */
  bool mShutdown = false;

  /**
   * Absolute value for when the last user interaction took place.
   */
  mozilla::TimeStamp mLastUserInteraction;

  /**
   * Function that ensures the timer is running with at least the minimum time
   * needed.  It will kill the timer if there are no active observers.
   */
  void ReconfigureTimer(void);

  /**
   * Callback function that is called when the internal timer expires.
   *
   * This in turn calls the IdleTimerCallback that does the real processing
   */
  static void StaticIdleTimerCallback(nsITimer* aTimer, void* aClosure);

  /**
   * Function that handles when a timer has expired
   */
  void IdleTimerCallback(void);
};

#endif  // nsUserIdleService_h_
