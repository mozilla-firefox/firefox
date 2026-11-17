# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import errno
import glob
import os
import shutil
import subprocess
from xml.sax.saxutils import quoteattr

from mozbuild.base import ExecutionSummary

from ..frontend.data import ComputedFlags
from .common import CommonBackend

# TODO Have ./mach eclipse generate the workspace and index it:
# /Users/bgirard/mozilla/eclipse/eclipse/eclipse/eclipse -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data $PWD/workspace -importAll $PWD/eclipse
# Open eclipse:
# /Users/bgirard/mozilla/eclipse/eclipse/eclipse/eclipse -data $PWD/workspace


class CppEclipseBackend(CommonBackend):
    """Backend that generates Cpp Eclipse project files."""

    def __init__(self, environment):
        if os.name == "nt":
            raise Exception(
                "Eclipse is not supported on Windows. "
                "Consider using Visual Studio instead."
            )
        super().__init__(environment)

    def _init(self):
        CommonBackend._init(self)

        self._args_for_dirs = {}
        self._project_name = "Gecko"
        self._workspace_dir = self._get_workspace_path()
        self._workspace_lang_dir = os.path.join(
            self._workspace_dir, ".metadata/.plugins/org.eclipse.cdt.core"
        )
        self._project_dir = os.path.join(self._workspace_dir, self._project_name)
        self._overwriting_workspace = os.path.isdir(self._workspace_dir)

        self._macbundle = self.environment.substs["MOZ_MACBUNDLE_NAME"]
        self._appname = self.environment.substs["MOZ_APP_NAME"]
        self._bin_suffix = self.environment.substs["BIN_SUFFIX"]
        self._cxx = self.environment.substs["CXX"]
        # Note: We need the C Pre Processor (CPP) flags, not the CXX flags
        self._cppflags = self.environment.substs.get("CPPFLAGS", "")

    def summary(self):
        return ExecutionSummary(
            "CppEclipse backend executed in {execution_time:.2f}s\n"
            'Generated Cpp Eclipse workspace in "{workspace:s}".\n'
            "If missing, import the project using File > Import > General > Existing Project into workspace\n"
            "\n"
            "Run with: eclipse -data {workspace:s}\n",
            execution_time=self._execution_time,
            workspace=self._workspace_dir,
        )

    def _get_workspace_path(self):
        return CppEclipseBackend.get_workspace_path(
            self.environment.topsrcdir, self.environment.topobjdir
        )

    @staticmethod
    def get_workspace_path(topsrcdir, topobjdir):
        # Eclipse doesn't support having the workspace inside the srcdir.
        # Since most people have their objdir inside their srcdir it's easier
        # and more consistent to just put the workspace along side the srcdir
        srcdir_parent = os.path.dirname(topsrcdir)
        workspace_dirname = "eclipse_" + os.path.basename(topobjdir)
        return os.path.join(srcdir_parent, workspace_dirname)

    def consume_object(self, obj):
        reldir = getattr(obj, "relsrcdir", None)

        # Note that unlike VS, Eclipse' indexer seem to crawl the headers and
        # isn't picky about the local includes.
        if isinstance(obj, ComputedFlags):
            args = self._args_for_dirs.setdefault(
                "tree/" + reldir, {"includes": [], "defines": []}
            )
            # use the same args for any objdirs we include:
            if reldir == "dom/bindings":
                self._args_for_dirs.setdefault("generated-webidl", args)
            if reldir == "ipc/ipdl":
                self._args_for_dirs.setdefault("generated-ipdl", args)

            includes = args["includes"]
            if "BASE_INCLUDES" in obj.flags and obj.flags["BASE_INCLUDES"]:
                includes += obj.flags["BASE_INCLUDES"]
            if "LOCAL_INCLUDES" in obj.flags and obj.flags["LOCAL_INCLUDES"]:
                includes += obj.flags["LOCAL_INCLUDES"]

            defs = args["defines"]
            if "DEFINES" in obj.flags and obj.flags["DEFINES"]:
                defs += obj.flags["DEFINES"]
            if "LIBRARY_DEFINES" in obj.flags and obj.flags["LIBRARY_DEFINES"]:
                defs += obj.flags["LIBRARY_DEFINES"]

        return True

    def consume_finished(self):
        settings_dir = os.path.join(self._project_dir, ".settings")
        launch_dir = os.path.join(self._project_dir, "RunConfigurations")
        workspace_settings_dir = os.path.join(
            self._workspace_dir, ".metadata/.plugins/org.eclipse.core.runtime/.settings"
        )

        for dir_name in [
            self._project_dir,
            settings_dir,
            launch_dir,
            workspace_settings_dir,
            self._workspace_lang_dir,
        ]:
            os.makedirs(dir_name, exist_ok=True)

        project_path = os.path.join(self._project_dir, ".project")
        with open(project_path, "w") as fh:
            self._write_project(fh)

        cproject_path = os.path.join(self._project_dir, ".cproject")
        with open(cproject_path, "w") as fh:
            self._write_cproject(fh)

        language_path = os.path.join(settings_dir, "language.settings.xml")
        with open(language_path, "w") as fh:
            self._write_language_settings(fh)

        workspace_language_path = os.path.join(
            self._workspace_lang_dir, "language.settings.xml"
        )
        with open(workspace_language_path, "w") as fh:
            workspace_lang_settings = WORKSPACE_LANGUAGE_SETTINGS_TEMPLATE
            workspace_lang_settings = workspace_lang_settings.replace(
                "@COMPILER_FLAGS@", self._cxx + " " + self._cppflags
            )
            fh.write(workspace_lang_settings)

        self._write_launch_files(launch_dir)

        core_resources_prefs_path = os.path.join(
            workspace_settings_dir, "org.eclipse.core.resources.prefs"
        )
        with open(core_resources_prefs_path, "w") as fh:
            fh.write(STATIC_CORE_RESOURCES_PREFS)

        core_runtime_prefs_path = os.path.join(
            workspace_settings_dir, "org.eclipse.core.runtime.prefs"
        )
        with open(core_runtime_prefs_path, "w") as fh:
            fh.write(STATIC_CORE_RUNTIME_PREFS)

        ui_prefs_path = os.path.join(workspace_settings_dir, "org.eclipse.ui.prefs")
        with open(ui_prefs_path, "w") as fh:
            fh.write(STATIC_UI_PREFS)

        cdt_ui_prefs_path = os.path.join(
            workspace_settings_dir, "org.eclipse.cdt.ui.prefs"
        )
        cdt_ui_prefs = STATIC_CDT_UI_PREFS
        # Here we generate the code formatter that will show up in the UI with
        # the name "Mozilla".  The formatter is stored as a single line of XML
        # in the org.eclipse.cdt.ui.formatterprofiles pref.
        cdt_ui_prefs += r'org.eclipse.cdt.ui.formatterprofiles=<?xml version\="1.0" encoding\="UTF-8" standalone\="no"?>\n<profiles version\="1">\n<profile kind\="CodeFormatterProfile" name\="Mozilla" version\="1">\n'
        XML_PREF_TEMPLATE = r'<setting id\="@PREF_NAME@" value\="@PREF_VAL@"/>\n'
        for line in FORMATTER_SETTINGS.splitlines():
            [pref, val] = line.split("=")
            cdt_ui_prefs += XML_PREF_TEMPLATE.replace("@PREF_NAME@", pref).replace(
                "@PREF_VAL@", val
            )
        cdt_ui_prefs += r"</profile>\n</profiles>\n"
        with open(cdt_ui_prefs_path, "w") as fh:
            fh.write(cdt_ui_prefs)

        cdt_core_prefs_path = os.path.join(
            workspace_settings_dir, "org.eclipse.cdt.core.prefs"
        )
        with open(cdt_core_prefs_path, "w") as fh:
            cdt_core_prefs = STATIC_CDT_CORE_PREFS
            # When we generated the code formatter called "Mozilla" above, we
            # also set it to be the active formatter.  When a formatter is set
            # as the active formatter all its prefs are set in this prefs file,
            # so we need add those now:
            cdt_core_prefs += FORMATTER_SETTINGS
            fh.write(cdt_core_prefs)

        editor_prefs_path = os.path.join(
            workspace_settings_dir, "org.eclipse.ui.editors.prefs"
        )
        with open(editor_prefs_path, "w") as fh:
            fh.write(EDITOR_SETTINGS)

        # Now import the project into the workspace
        self._import_project()

    def _import_project(self):
        # If the workspace already exists then don't import the project again because
        # eclipse doesn't handle this properly
        if self._overwriting_workspace:
            return

        # We disable the indexer otherwise we're forced to index
        # the whole codebase when importing the project. Indexing the project can take 20 minutes.
        self._write_noindex()

        try:
            subprocess.check_call([
                "eclipse",
                "-application",
                "-nosplash",
                "org.eclipse.cdt.managedbuilder.core.headlessbuild",
                "-data",
                self._workspace_dir,
                "-importAll",
                self._project_dir,
            ])
        except OSError as e:
            # Remove the workspace directory so we re-generate it and
            # try to import again when the backend is invoked again.
            shutil.rmtree(self._workspace_dir)

            if e.errno == errno.ENOENT:
                raise Exception(
                    "Failed to launch eclipse to import project. "
                    "Ensure 'eclipse' is in your PATH and try again"
                )
            else:
                raise
        finally:
            self._remove_noindex()

    def _write_noindex(self):
        noindex_path = os.path.join(
            self._project_dir, ".settings/org.eclipse.cdt.core.prefs"
        )
        with open(noindex_path, "w") as fh:
            fh.write(NOINDEX_TEMPLATE)

    def _remove_noindex(self):
        # Below we remove the config file that temporarily disabled the indexer
        # while we were importing the project. Unfortunately, CDT doesn't
        # notice indexer settings changes in config files when it restarts. To
        # work around that we remove the index database here to force it to:
        for f in glob.glob(os.path.join(self._workspace_lang_dir, "Gecko.*.pdom")):
            os.remove(f)

        noindex_path = os.path.join(
            self._project_dir, ".settings/org.eclipse.cdt.core.prefs"
        )
        # This may fail if the entire tree has been removed; that's fine.
        try:
            os.remove(noindex_path)
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise

    def _write_language_settings(self, fh):
        def add_abs_include_path(absinclude):
            assert absinclude[:3] == "-I/"
            return LANGUAGE_SETTINGS_TEMPLATE_DIR_INCLUDE.replace(
                "@INCLUDE_PATH@", absinclude[2:]
            )

        def add_objdir_include_path(relpath):
            p = os.path.join(self.environment.topobjdir, relpath)
            return LANGUAGE_SETTINGS_TEMPLATE_DIR_INCLUDE.replace("@INCLUDE_PATH@", p)

        def add_define(name, value):
            define = LANGUAGE_SETTINGS_TEMPLATE_DIR_DEFINE
            define = define.replace("@NAME@", name)
            # We use quoteattr here because some defines contain characters
            # such as "<" and '"' which need proper XML escaping.
            define = define.replace("@VALUE@", quoteattr(value))
            return define

        fh.write(LANGUAGE_SETTINGS_TEMPLATE_HEADER)

        # Unfortunately, whenever we set a user defined include path or define
        # on a directory, Eclipse ignores user defined include paths and defines
        # on ancestor directories.  That means that we need to add all the
        # common include paths and defines to every single directory entry that
        # we add settings for.  (Fortunately that doesn't appear to have a
        # noticeable impact on the time it takes to open the generated Eclipse
        # project.)  We do that by generating a template here that we can then
        # use for each individual directory in the loop below.
        #
        dirsettings_template = LANGUAGE_SETTINGS_TEMPLATE_DIR_HEADER

        # Add OS_COMPILE_CXXFLAGS args (same as OS_COMPILE_CFLAGS):
        dirsettings_template = dirsettings_template.replace(
            "@PREINCLUDE_FILE_PATH@",
            os.path.join(self.environment.topobjdir, "dist/include/mozilla-config.h"),
        )
        dirsettings_template += add_define("MOZILLA_CLIENT", "1")

        # Add EXTRA_INCLUDES args:
        dirsettings_template += add_objdir_include_path("dist/include")

        # Add OS_INCLUDES args:
        # XXX media/webrtc/trunk/webrtc's moz.builds reset this.
        dirsettings_template += add_objdir_include_path("dist/include/nspr")
        dirsettings_template += add_objdir_include_path("dist/include/nss")

        # Finally, add anything else that makes things work better.
        #
        # Because of https://developer.mozilla.org/en-US/docs/Eclipse_CDT#Headers_are_only_parsed_once
        # we set MOZILLA_INTERNAL_API for all directories to make sure
        # headers are indexed with MOZILLA_INTERNAL_API set.  Unfortunately
        # this means that MOZILLA_EXTERNAL_API code will suffer.
        #
        # TODO: If we're doing this for MOZILLA_EXTERNAL_API then we may want
        # to do it for other LIBRARY_DEFINES's defines too.  Well, at least for
        # STATIC_EXPORTABLE_JS_API which may be important to JS people.
        # (The other two LIBRARY_DEFINES defines -- MOZ_HAS_MOZGLUE and
        # IMPL_LIBXUL -- don't affect much and probably don't matter to anyone).
        #
        # TODO: Should we also always set DEBUG so that DEBUG code is always
        # indexed?  Or is there significant amounts of non-DEBUG code that
        # would be adversely affected?
        #
        # TODO: Investigate whether the ordering of directories in the project
        # file can be used to our advantage so that the first indexing of
        # important headers has the defines we want.
        #
        dirsettings_template += add_objdir_include_path("ipc/ipdl/_ipdlheaders")
        dirsettings_template += add_define("MOZILLA_INTERNAL_API", "1")

        for path, args in self._args_for_dirs.items():
            dirsettings = dirsettings_template
            dirsettings = dirsettings.replace("@RELATIVE_PATH@", path)
            for i in args["includes"]:
                dirsettings += add_abs_include_path(i)
            for d in args["defines"]:
                assert d[:2] in {"-D", "-U"}
                if d[:2] == "-U":
                    # gfx/harfbuzz/src uses -UDEBUG, at least on Mac
                    # XXX We should make this code smart enough to remove existing defines.
                    continue
                d = d[2:]  # get rid of leading "-D"
                name_value = d.split("=", 1)
                name = name_value[0]
                value = ""
                if len(name_value) == 2:
                    value = name_value[1]
                dirsettings += add_define(name, str(value))
            dirsettings += LANGUAGE_SETTINGS_TEMPLATE_DIR_FOOTER
            fh.write(dirsettings)

        fh.write(
            LANGUAGE_SETTINGS_TEMPLATE_FOOTER.replace(
                "@COMPILER_FLAGS@", self._cxx + " " + self._cppflags
            )
        )

    def _write_launch_files(self, launch_dir):
        bin_dir = os.path.join(self.environment.topobjdir, "dist")

        # TODO Improve binary detection
        if self._macbundle:
            exe_path = os.path.join(bin_dir, self._macbundle, "Contents/MacOS")
        else:
            exe_path = os.path.join(bin_dir, "bin")

        exe_path = os.path.join(exe_path, self._appname + self._bin_suffix)

        main_gecko_launch = os.path.join(launch_dir, "gecko.launch")
        with open(main_gecko_launch, "w") as fh:
            launch = GECKO_LAUNCH_CONFIG_TEMPLATE
            launch = launch.replace("@LAUNCH_PROGRAM@", exe_path)
            launch = launch.replace("@LAUNCH_ARGS@", "-P -no-remote")
            fh.write(launch)

        # TODO Add more launch configs (and delegate calls to mach)

    def _write_project(self, fh):
        project = PROJECT_TEMPLATE

        project = project.replace("@PROJECT_NAME@", self._project_name)
        project = project.replace("@PROJECT_TOPSRCDIR@", self.environment.topsrcdir)
        project = project.replace(
            "@GENERATED_IPDL_FILES@",
            os.path.join(self.environment.topobjdir, "ipc", "ipdl"),
        )
        project = project.replace(
            "@GENERATED_WEBIDL_FILES@",
            os.path.join(self.environment.topobjdir, "dom", "bindings"),
        )
        fh.write(project)

    def _write_cproject(self, fh):
        cproject_header = CPROJECT_TEMPLATE_HEADER
        cproject_header = cproject_header.replace(
            "@PROJECT_TOPSRCDIR@", self.environment.topobjdir
        )
        cproject_header = cproject_header.replace(
            "@MACH_COMMAND@", os.path.join(self.environment.topsrcdir, "mach")
        )
        fh.write(cproject_header)
        fh.write(CPROJECT_TEMPLATE_FOOTER)


PROJECT_TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<projectDescription>
        <name>@PROJECT_NAME@</name>
        <comment></comment>
        <projects>
        </projects>
        <buildSpec>
                <buildCommand>
                        <name>org.eclipse.cdt.managedbuilder.core.genmakebuilder</name>
                        <triggers>clean,full,incremental,</triggers>
                        <arguments>
                        </arguments>
                </buildCommand>
                <buildCommand>
                        <name>org.eclipse.cdt.managedbuilder.core.ScannerConfigBuilder</name>
                        <triggers></triggers>
            <arguments>
            </arguments>
                </buildCommand>
        </buildSpec>
        <natures>
                <nature>org.eclipse.cdt.core.cnature</nature>
                <nature>org.eclipse.cdt.core.ccnature</nature>
                <nature>org.eclipse.cdt.managedbuilder.core.managedBuildNature</nature>
                <nature>org.eclipse.cdt.managedbuilder.core.ScannerConfigNature</nature>
        </natures>
        <linkedResources>
                <link>
                        <name>tree</name>
                        <type>2</type>
                        <location>@PROJECT_TOPSRCDIR@</location>
                </link>
                <link>
                        <name>generated-ipdl</name>
                        <type>2</type>
                        <location>@GENERATED_IPDL_FILES@</location>
                </link>
                <link>
                        <name>generated-webidl</name>
                        <type>2</type>
                        <location>@GENERATED_WEBIDL_FILES@</location>
                </link>
        </linkedResources>
        <filteredResources>
                <filter>
                        <id>17111971</id>
                        <name>tree</name>
                        <type>30</type>
                        <matcher>
                                <id>org.eclipse.ui.ide.multiFilter</id>
                                <arguments>1.0-name-matches-false-false-obj-*</arguments>
                        </matcher>
                </filter>
                <filter>
                        <id>14081994</id>
                        <name>tree</name>
                        <type>22</type>
                        <matcher>
                                <id>org.eclipse.ui.ide.multiFilter</id>
                                <arguments>1.0-name-matches-false-false-*.rej</arguments>
                        </matcher>
                </filter>
                <filter>
                        <id>25121970</id>
                        <name>tree</name>
                        <type>22</type>
                        <matcher>
                                <id>org.eclipse.ui.ide.multiFilter</id>
                                <arguments>1.0-name-matches-false-false-*.orig</arguments>
                        </matcher>
                </filter>
                <filter>
                        <id>10102004</id>
                        <name>tree</name>
                        <type>10</type>
                        <matcher>
                                <id>org.eclipse.ui.ide.multiFilter</id>
                                <arguments>1.0-name-matches-false-false-.hg</arguments>
                        </matcher>
                </filter>
                <filter>
                        <id>23122002</id>
                        <name>tree</name>
                        <type>22</type>
                        <matcher>
                                <id>org.eclipse.ui.ide.multiFilter</id>
                                <arguments>1.0-name-matches-false-false-*.pyc</arguments>
                        </matcher>
                </filter>
        </filteredResources>
</projectDescription>
"""

CPROJECT_TEMPLATE_HEADER = """<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<?fileVersion 4.0.0?>

<cproject storage_type_id="org.eclipse.cdt.core.XmlProjectDescriptionStorage">
        <storageModule moduleId="org.eclipse.cdt.core.settings">
                <cconfiguration id="0.1674256904">
                        <storageModule buildSystemId="org.eclipse.cdt.managedbuilder.core.configurationDataProvider" id="0.1674256904" moduleId="org.eclipse.cdt.core.settings" name="Default">
                                <externalSettings/>
                                <extensions>
                                        <extension id="org.eclipse.cdt.core.VCErrorParser" point="org.eclipse.cdt.core.ErrorParser"/>
                                        <extension id="org.eclipse.cdt.core.GmakeErrorParser" point="org.eclipse.cdt.core.ErrorParser"/>
                                        <extension id="org.eclipse.cdt.core.CWDLocator" point="org.eclipse.cdt.core.ErrorParser"/>
                                        <extension id="org.eclipse.cdt.core.GCCErrorParser" point="org.eclipse.cdt.core.ErrorParser"/>
                                        <extension id="org.eclipse.cdt.core.GASErrorParser" point="org.eclipse.cdt.core.ErrorParser"/>
                                        <extension id="org.eclipse.cdt.core.GLDErrorParser" point="org.eclipse.cdt.core.ErrorParser"/>
                                </extensions>
                        </storageModule>
                        <storageModule moduleId="cdtBuildSystem" version="4.0.0">
                                <configuration artifactName="${ProjName}" buildProperties="" description="" id="0.1674256904" name="Default" parent="org.eclipse.cdt.build.core.prefbase.cfg">
                                        <folderInfo id="0.1674256904." name="/" resourcePath="">
                                                <toolChain id="cdt.managedbuild.toolchain.gnu.cross.exe.debug.1276586933" name="Cross GCC" superClass="cdt.managedbuild.toolchain.gnu.cross.exe.debug">
                                                        <targetPlatform archList="all" binaryParser="" id="cdt.managedbuild.targetPlatform.gnu.cross.710759961" isAbstract="false" osList="all" superClass="cdt.managedbuild.targetPlatform.gnu.cross"/>
                            <builder arguments="--log-no-times build" buildPath="@PROJECT_TOPSRCDIR@" command="@MACH_COMMAND@" enableCleanBuild="false" incrementalBuildTarget="binaries" id="org.eclipse.cdt.build.core.settings.default.builder.1437267827" keepEnvironmentInBuildfile="false" name="Gnu Make Builder" superClass="org.eclipse.cdt.build.core.settings.default.builder"/>
                                                </toolChain>
                                        </folderInfo>
"""
CPROJECT_TEMPLATE_FILEINFO = """                                        <fileInfo id="0.1674256904.474736658" name="Layers.cpp" rcbsApplicability="disable" resourcePath="tree/gfx/layers/Layers.cpp" toolsToInvoke="org.eclipse.cdt.build.core.settings.holder.582514939.463639939">
                                                <tool id="org.eclipse.cdt.build.core.settings.holder.582514939.463639939" name="GNU C++" superClass="org.eclipse.cdt.build.core.settings.holder.582514939">
                                                        <option id="org.eclipse.cdt.build.core.settings.holder.symbols.232300236" superClass="org.eclipse.cdt.build.core.settings.holder.symbols" valueType="definedSymbols">
                                                                <listOptionValue builtIn="false" value="BENWA=BENWAVAL"/>
                                                        </option>
                                                        <inputType id="org.eclipse.cdt.build.core.settings.holder.inType.1942876228" languageId="org.eclipse.cdt.core.g++" languageName="GNU C++" sourceContentType="org.eclipse.cdt.core.cxxSource,org.eclipse.cdt.core.cxxHeader" superClass="org.eclipse.cdt.build.core.settings.holder.inType"/>
                                                </tool>
                                        </fileInfo>
"""
CPROJECT_TEMPLATE_FOOTER = """
                    <sourceEntries>
                        <entry excluding="**/lib*|**/third_party/|tree/*.xcodeproj/|tree/.cargo/|tree/.vscode/|tree/build/|tree/extensions/|tree/gfx/angle/|tree/gfx/cairo/|tree/gfx/skia/skia/|tree/intl/icu/|tree/js/|tree/media/|tree/modules/freetype2|tree/modules/pdfium/|tree/netwerk/|tree/netwerk/sctp|tree/netwerk/srtp|tree/nsprpub/lib|tree/nsprpub/pr/src|tree/other-licenses/|tree/parser/|tree/python/|tree/security/nss/|tree/tools/" flags="VALUE_WORKSPACE_PATH" kind="sourcePath" name=""/>
                    </sourceEntries>
                                </configuration>
                        </storageModule>
                        <storageModule moduleId="org.eclipse.cdt.core.externalSettings"/>
                </cconfiguration>
        </storageModule>
        <storageModule moduleId="cdtBuildSystem" version="4.0.0">
                <project id="Empty.null.1281234804" name="Empty"/>
        </storageModule>
        <storageModule moduleId="scannerConfiguration">
                <autodiscovery enabled="true" problemReportingEnabled="true" selectedProfileId=""/>
                <scannerConfigBuildInfo instanceId="0.1674256904">
                        <autodiscovery enabled="true" problemReportingEnabled="true" selectedProfileId=""/>
                </scannerConfigBuildInfo>
        </storageModule>
        <storageModule moduleId="refreshScope" versionNumber="2">
                <configuration configurationName="Default"/>
        </storageModule>
        <storageModule moduleId="org.eclipse.cdt.core.LanguageSettingsProviders"/>
</cproject>
"""

WORKSPACE_LANGUAGE_SETTINGS_TEMPLATE = """<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<plugin>
        <extension point="org.eclipse.cdt.core.LanguageSettingsProvider">
                <provider class="org.eclipse.cdt.managedbuilder.language.settings.providers.GCCBuiltinSpecsDetector" console="true" id="org.eclipse.cdt.managedbuilder.core.GCCBuiltinSpecsDetector" keep-relative-paths="false" name="CDT GCC Built-in Compiler Settings" parameter="@COMPILER_FLAGS@ -E -P -v -dD &quot;${INPUTS}&quot;">
                        <language-scope id="org.eclipse.cdt.core.gcc"/>
                        <language-scope id="org.eclipse.cdt.core.g++"/>
                </provider>
        </extension>
</plugin>
"""


# The settings set via this template can be found in the UI by opening
# the Properties for a directory in the Project Explorer tab, then going to
# C/C++ General > Preprocessor Include Paths, Macros, etc., selecting the
# C++ item from the Languages column, and then expanding the
# CDT User Settings Entries item to the right.

LANGUAGE_SETTINGS_TEMPLATE_HEADER = """<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<project>
    <configuration id="0.1674256904" name="Default">
        <extension point="org.eclipse.cdt.core.LanguageSettingsProvider">
            <provider class="org.eclipse.cdt.core.language.settings.providers.LanguageSettingsGenericProvider" id="org.eclipse.cdt.ui.UserLanguageSettingsProvider" name="CDT User Setting Entries" prefer-non-shared="true" store-entries-with-project="true">
                <language id="org.eclipse.cdt.core.g++">
"""

LANGUAGE_SETTINGS_TEMPLATE_DIR_HEADER = """                 <resource project-relative-path="@RELATIVE_PATH@">
                        <entry kind="includeFile" name="@PREINCLUDE_FILE_PATH@">
                            <flag value="LOCAL"/>
                        </entry>
"""

LANGUAGE_SETTINGS_TEMPLATE_DIR_INCLUDE = """                        <entry kind="includePath" name="@INCLUDE_PATH@">
                            <flag value="LOCAL"/>
                        </entry>
"""

LANGUAGE_SETTINGS_TEMPLATE_DIR_DEFINE = """                        <entry kind="macro" name="@NAME@" value=@VALUE@/>
"""

LANGUAGE_SETTINGS_TEMPLATE_DIR_FOOTER = """                    </resource>
"""

LANGUAGE_SETTINGS_TEMPLATE_FOOTER = """                </language>
                </provider>
            <provider class="org.eclipse.cdt.internal.build.crossgcc.CrossGCCBuiltinSpecsDetector" console="false" env-hash="-859273372804152468" id="org.eclipse.cdt.build.crossgcc.CrossGCCBuiltinSpecsDetector" keep-relative-paths="false" name="CDT Cross GCC Built-in Compiler Settings" parameter="@COMPILER_FLAGS@ -E -P -v -dD &quot;${INPUTS}&quot; -std=c++11" prefer-non-shared="true" store-entries-with-project="true">
                <language-scope id="org.eclipse.cdt.core.gcc"/>
                <language-scope id="org.eclipse.cdt.core.g++"/>
            </provider>
            <provider-reference id="org.eclipse.cdt.managedbuilder.core.MBSLanguageSettingsProvider" ref="shared-provider"/>
        </extension>
    </configuration>
</project>
"""


GECKO_LAUNCH_CONFIG_TEMPLATE = """<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<launchConfiguration type="org.eclipse.cdt.launch.applicationLaunchType">
<booleanAttribute key="org.eclipse.cdt.dsf.gdb.AUTO_SOLIB" value="true"/>
<listAttribute key="org.eclipse.cdt.dsf.gdb.AUTO_SOLIB_LIST"/>
<stringAttribute key="org.eclipse.cdt.dsf.gdb.DEBUG_NAME" value="lldb"/>
<booleanAttribute key="org.eclipse.cdt.dsf.gdb.DEBUG_ON_FORK" value="false"/>
<stringAttribute key="org.eclipse.cdt.dsf.gdb.GDB_INIT" value=""/>
<booleanAttribute key="org.eclipse.cdt.dsf.gdb.NON_STOP" value="false"/>
<booleanAttribute key="org.eclipse.cdt.dsf.gdb.REVERSE" value="false"/>
<listAttribute key="org.eclipse.cdt.dsf.gdb.SOLIB_PATH"/>
<stringAttribute key="org.eclipse.cdt.dsf.gdb.TRACEPOINT_MODE" value="TP_NORMAL_ONLY"/>
<booleanAttribute key="org.eclipse.cdt.dsf.gdb.UPDATE_THREADLIST_ON_SUSPEND" value="false"/>
<booleanAttribute key="org.eclipse.cdt.dsf.gdb.internal.ui.launching.LocalApplicationCDebuggerTab.DEFAULTS_SET" value="true"/>
<intAttribute key="org.eclipse.cdt.launch.ATTR_BUILD_BEFORE_LAUNCH_ATTR" value="2"/>
<stringAttribute key="org.eclipse.cdt.launch.COREFILE_PATH" value=""/>
<stringAttribute key="org.eclipse.cdt.launch.DEBUGGER_ID" value="gdb"/>
<stringAttribute key="org.eclipse.cdt.launch.DEBUGGER_START_MODE" value="run"/>
<booleanAttribute key="org.eclipse.cdt.launch.DEBUGGER_STOP_AT_MAIN" value="false"/>
<stringAttribute key="org.eclipse.cdt.launch.DEBUGGER_STOP_AT_MAIN_SYMBOL" value="main"/>
<stringAttribute key="org.eclipse.cdt.launch.PROGRAM_ARGUMENTS" value="@LAUNCH_ARGS@"/>
<stringAttribute key="org.eclipse.cdt.launch.PROGRAM_NAME" value="@LAUNCH_PROGRAM@"/>
<stringAttribute key="org.eclipse.cdt.launch.PROJECT_ATTR" value="Gecko"/>
<booleanAttribute key="org.eclipse.cdt.launch.PROJECT_BUILD_CONFIG_AUTO_ATTR" value="true"/>
<stringAttribute key="org.eclipse.cdt.launch.PROJECT_BUILD_CONFIG_ID_ATTR" value=""/>
<booleanAttribute key="org.eclipse.cdt.launch.use_terminal" value="true"/>
<listAttribute key="org.eclipse.debug.core.MAPPED_RESOURCE_PATHS">
<listEntry value="/gecko"/>
</listAttribute>
<listAttribute key="org.eclipse.debug.core.MAPPED_RESOURCE_TYPES">
<listEntry value="4"/>
</listAttribute>
<booleanAttribute key="org.eclipse.debug.ui.ATTR_LAUNCH_IN_BACKGROUND" value="false"/>
<stringAttribute key="process_factory_id" value="org.eclipse.cdt.dsf.gdb.GdbProcessFactory"/>
</launchConfiguration>
"""


EDITOR_SETTINGS = """eclipse.preferences.version=1
lineNumberRuler=true
overviewRuler_migration=migrated_3.1
printMargin=true
printMarginColumn=80
showCarriageReturn=false
showEnclosedSpaces=false
showLeadingSpaces=false
showLineFeed=false
showWhitespaceCharacters=true
spacesForTabs=true
tabWidth=2
undoHistorySize=200
"""


STATIC_CORE_RESOURCES_PREFS = """eclipse.preferences.version=1
refresh.enabled=true
"""

STATIC_CORE_RUNTIME_PREFS = """eclipse.preferences.version=1
content-types/org.eclipse.cdt.core.cxxSource/file-extensions=mm
content-types/org.eclipse.core.runtime.xml/file-extensions=xul
content-types/org.eclipse.wst.jsdt.core.jsSource/file-extensions=jsm
"""

STATIC_UI_PREFS = """eclipse.preferences.version=1
showIntro=false
"""

STATIC_CDT_CORE_PREFS = """eclipse.preferences.version=1
indexer.updatePolicy=0
"""

FORMATTER_SETTINGS = """org.eclipse.cdt.core.formatter.alignment_for_arguments_in_method_invocation=16
org.eclipse.cdt.core.formatter.alignment_for_assignment=16
org.eclipse.cdt.core.formatter.alignment_for_base_clause_in_type_declaration=80
org.eclipse.cdt.core.formatter.alignment_for_binary_expression=16
org.eclipse.cdt.core.formatter.alignment_for_compact_if=16
org.eclipse.cdt.core.formatter.alignment_for_conditional_expression=34
org.eclipse.cdt.core.formatter.alignment_for_conditional_expression_chain=18
org.eclipse.cdt.core.formatter.alignment_for_constructor_initializer_list=48
org.eclipse.cdt.core.formatter.alignment_for_declarator_list=16
org.eclipse.cdt.core.formatter.alignment_for_enumerator_list=48
org.eclipse.cdt.core.formatter.alignment_for_expression_list=0
org.eclipse.cdt.core.formatter.alignment_for_expressions_in_array_initializer=16
org.eclipse.cdt.core.formatter.alignment_for_member_access=0
org.eclipse.cdt.core.formatter.alignment_for_overloaded_left_shift_chain=16
org.eclipse.cdt.core.formatter.alignment_for_parameters_in_method_declaration=16
org.eclipse.cdt.core.formatter.alignment_for_throws_clause_in_method_declaration=16
org.eclipse.cdt.core.formatter.brace_position_for_array_initializer=end_of_line
org.eclipse.cdt.core.formatter.brace_position_for_block=end_of_line
org.eclipse.cdt.core.formatter.brace_position_for_block_in_case=next_line_shifted
org.eclipse.cdt.core.formatter.brace_position_for_method_declaration=next_line
org.eclipse.cdt.core.formatter.brace_position_for_namespace_declaration=end_of_line
org.eclipse.cdt.core.formatter.brace_position_for_switch=end_of_line
org.eclipse.cdt.core.formatter.brace_position_for_type_declaration=next_line
org.eclipse.cdt.core.formatter.comment.min_distance_between_code_and_line_comment=1
org.eclipse.cdt.core.formatter.comment.never_indent_line_comments_on_first_column=true
org.eclipse.cdt.core.formatter.comment.preserve_white_space_between_code_and_line_comments=true
org.eclipse.cdt.core.formatter.compact_else_if=true
org.eclipse.cdt.core.formatter.continuation_indentation=2
org.eclipse.cdt.core.formatter.continuation_indentation_for_array_initializer=2
org.eclipse.cdt.core.formatter.format_guardian_clause_on_one_line=false
org.eclipse.cdt.core.formatter.indent_access_specifier_compare_to_type_header=false
org.eclipse.cdt.core.formatter.indent_access_specifier_extra_spaces=0
org.eclipse.cdt.core.formatter.indent_body_declarations_compare_to_access_specifier=true
org.eclipse.cdt.core.formatter.indent_body_declarations_compare_to_namespace_header=false
org.eclipse.cdt.core.formatter.indent_breaks_compare_to_cases=true
org.eclipse.cdt.core.formatter.indent_declaration_compare_to_template_header=true
org.eclipse.cdt.core.formatter.indent_empty_lines=false
org.eclipse.cdt.core.formatter.indent_statements_compare_to_block=true
org.eclipse.cdt.core.formatter.indent_statements_compare_to_body=true
org.eclipse.cdt.core.formatter.indent_switchstatements_compare_to_cases=true
org.eclipse.cdt.core.formatter.indent_switchstatements_compare_to_switch=false
org.eclipse.cdt.core.formatter.indentation.size=2
org.eclipse.cdt.core.formatter.insert_new_line_after_opening_brace_in_array_initializer=do not insert
org.eclipse.cdt.core.formatter.insert_new_line_after_template_declaration=insert
org.eclipse.cdt.core.formatter.insert_new_line_at_end_of_file_if_missing=do not insert
org.eclipse.cdt.core.formatter.insert_new_line_before_catch_in_try_statement=do not insert
org.eclipse.cdt.core.formatter.insert_new_line_before_closing_brace_in_array_initializer=do not insert
org.eclipse.cdt.core.formatter.insert_new_line_before_colon_in_constructor_initializer_list=do not insert
org.eclipse.cdt.core.formatter.insert_new_line_before_else_in_if_statement=do not insert
org.eclipse.cdt.core.formatter.insert_new_line_before_identifier_in_function_declaration=insert
org.eclipse.cdt.core.formatter.insert_new_line_before_while_in_do_statement=do not insert
org.eclipse.cdt.core.formatter.insert_new_line_in_empty_block=insert
org.eclipse.cdt.core.formatter.insert_space_after_assignment_operator=insert
org.eclipse.cdt.core.formatter.insert_space_after_binary_operator=insert
org.eclipse.cdt.core.formatter.insert_space_after_closing_angle_bracket_in_template_arguments=insert
org.eclipse.cdt.core.formatter.insert_space_after_closing_angle_bracket_in_template_parameters=insert
org.eclipse.cdt.core.formatter.insert_space_after_closing_brace_in_block=insert
org.eclipse.cdt.core.formatter.insert_space_after_closing_paren_in_cast=insert
org.eclipse.cdt.core.formatter.insert_space_after_colon_in_base_clause=insert
org.eclipse.cdt.core.formatter.insert_space_after_colon_in_case=insert
org.eclipse.cdt.core.formatter.insert_space_after_colon_in_conditional=insert
org.eclipse.cdt.core.formatter.insert_space_after_colon_in_labeled_statement=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_array_initializer=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_base_types=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_declarator_list=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_enum_declarations=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_expression_list=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_method_declaration_parameters=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_method_declaration_throws=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_method_invocation_arguments=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_template_arguments=insert
org.eclipse.cdt.core.formatter.insert_space_after_comma_in_template_parameters=insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_angle_bracket_in_template_arguments=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_angle_bracket_in_template_parameters=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_brace_in_array_initializer=insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_bracket=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_cast=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_catch=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_exception_specification=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_for=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_if=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_method_declaration=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_method_invocation=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_parenthesized_expression=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_switch=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_opening_paren_in_while=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_postfix_operator=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_prefix_operator=do not insert
org.eclipse.cdt.core.formatter.insert_space_after_question_in_conditional=insert
org.eclipse.cdt.core.formatter.insert_space_after_semicolon_in_for=insert
org.eclipse.cdt.core.formatter.insert_space_after_unary_operator=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_assignment_operator=insert
org.eclipse.cdt.core.formatter.insert_space_before_binary_operator=insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_angle_bracket_in_template_arguments=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_angle_bracket_in_template_parameters=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_brace_in_array_initializer=insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_bracket=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_cast=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_catch=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_exception_specification=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_for=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_if=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_method_declaration=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_method_invocation=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_parenthesized_expression=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_switch=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_closing_paren_in_while=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_colon_in_base_clause=insert
org.eclipse.cdt.core.formatter.insert_space_before_colon_in_case=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_colon_in_conditional=insert
org.eclipse.cdt.core.formatter.insert_space_before_colon_in_default=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_colon_in_labeled_statement=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_array_initializer=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_base_types=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_declarator_list=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_enum_declarations=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_expression_list=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_method_declaration_parameters=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_method_declaration_throws=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_method_invocation_arguments=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_template_arguments=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_comma_in_template_parameters=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_angle_bracket_in_template_arguments=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_angle_bracket_in_template_parameters=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_brace_in_array_initializer=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_brace_in_block=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_brace_in_method_declaration=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_brace_in_namespace_declaration=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_brace_in_switch=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_brace_in_type_declaration=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_bracket=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_paren_in_catch=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_paren_in_exception_specification=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_paren_in_for=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_paren_in_if=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_paren_in_method_declaration=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_paren_in_method_invocation=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_paren_in_parenthesized_expression=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_paren_in_switch=insert
org.eclipse.cdt.core.formatter.insert_space_before_opening_paren_in_while=insert
org.eclipse.cdt.core.formatter.insert_space_before_postfix_operator=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_prefix_operator=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_question_in_conditional=insert
org.eclipse.cdt.core.formatter.insert_space_before_semicolon=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_semicolon_in_for=do not insert
org.eclipse.cdt.core.formatter.insert_space_before_unary_operator=do not insert
org.eclipse.cdt.core.formatter.insert_space_between_empty_braces_in_array_initializer=do not insert
org.eclipse.cdt.core.formatter.insert_space_between_empty_brackets=do not insert
org.eclipse.cdt.core.formatter.insert_space_between_empty_parens_in_exception_specification=do not insert
org.eclipse.cdt.core.formatter.insert_space_between_empty_parens_in_method_declaration=do not insert
org.eclipse.cdt.core.formatter.insert_space_between_empty_parens_in_method_invocation=do not insert
org.eclipse.cdt.core.formatter.join_wrapped_lines=false
org.eclipse.cdt.core.formatter.keep_else_statement_on_same_line=false
org.eclipse.cdt.core.formatter.keep_empty_array_initializer_on_one_line=false
org.eclipse.cdt.core.formatter.keep_imple_if_on_one_line=false
org.eclipse.cdt.core.formatter.keep_then_statement_on_same_line=false
org.eclipse.cdt.core.formatter.lineSplit=80
org.eclipse.cdt.core.formatter.number_of_empty_lines_to_preserve=1
org.eclipse.cdt.core.formatter.put_empty_statement_on_new_line=true
org.eclipse.cdt.core.formatter.tabulation.char=space
org.eclipse.cdt.core.formatter.tabulation.size=2
org.eclipse.cdt.core.formatter.use_tabs_only_for_leading_indentations=false
"""

STATIC_CDT_UI_PREFS = """eclipse.preferences.version=1
buildConsoleLines=10000
Console.limitConsoleOutput=false
ensureNewlineAtEOF=false
formatter_profile=_Mozilla
formatter_settings_version=1
org.eclipse.cdt.ui.formatterprofiles.version=1
removeTrailingWhitespace=true
removeTrailingWhitespaceEditedLines=true
scalability.numberOfLines=15000
markOccurrences=true
markOverloadedOperatorsOccurrences=true
stickyOccurrences=false
"""

NOINDEX_TEMPLATE = """eclipse.preferences.version=1
indexer/indexerId=org.eclipse.cdt.core.nullIndexer
"""
