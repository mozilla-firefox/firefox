/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that the StoreID in prefs isn't set in profiles.ini.
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

    Services.prefs.setCharPref("toolkit.profiles.storeID", "bishbashbosh");

    let service = getProfileService();
    let { profile } = selectStartupProfile();

    Assert.equal(profile.rootDir.path, defaultProfile.path);
    Assert.equal(service.currentProfile, profile);
    Assert.ok(!service.groupProfile);
    Assert.ok(!profile.storeID);

    checkProfileService();
  }
);
