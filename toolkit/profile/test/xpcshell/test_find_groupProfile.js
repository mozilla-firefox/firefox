/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Tests that the group profile is correctly found from the store ID.
 */
add_task(
  {
    skip_if: () => !AppConstants.MOZ_SELECTABLE_PROFILES,
  },
  async () => {
    let hash = xreDirProvider.getInstallHash();
    let defaultProfile = makeRandomProfileDir("default");
    let otherProfile = makeRandomProfileDir("other");
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

    Services.prefs.setCharPref("toolkit.profiles.storeID", "bishbashbosh");

    let service = getProfileService();
    let { profile } = selectStartupProfile(["-profile", otherProfile.path]);

    Assert.ok(!profile);
    Assert.ok(!service.currentProfile);
    Assert.ok(service.groupProfile);
    Assert.equal(service.groupProfile.storeID, "bishbashbosh");
    Assert.equal(service.groupProfile.rootDir.path, defaultProfile.path);

    checkProfileService();
  }
);
