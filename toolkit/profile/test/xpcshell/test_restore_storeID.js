/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that the StoreID is restored if in profiles.ini.
 */
add_task(
  {
    skip_if: () => !AppConstants.MOZ_SELECTABLE_PROFILES,
  },
  async () => {
    let hash = xreDirProvider.getInstallHash();
    let defaultProfile = makeRandomProfileDir("default");
    let profilesIni = {
      profiles: [
        {
          name: "default",
          path: defaultProfile.leafName,
          storeID: "bishbashbosh",
          default: true,
        },
      ],
      installs: {
        [hash]: {
          default: defaultProfile.leafName,
        },
      },
    };
    writeProfilesIni(profilesIni);

    let service = getProfileService();
    let { profile } = selectStartupProfile();

    let storeID = Services.prefs.getCharPref("toolkit.profiles.storeID");

    Assert.equal(profile.rootDir.path, defaultProfile.path);
    Assert.equal(service.currentProfile, profile);
    Assert.equal(service.groupProfile, profile);
    Assert.equal(profile.storeID, "bishbashbosh");
    Assert.equal(storeID, "bishbashbosh");

    checkProfileService();
  }
);
