import glob
import os
import unittest

from IPDLCompile import IPDLCompile


class IPDLTestCase(unittest.TestCase):
    def __init__(self, ipdlargv, filename):
        unittest.TestCase.__init__(self, "test")
        self.filename = filename
        self.compile = IPDLCompile(filename, ipdlargv)

    def test(self):
        self.compile.run()
        self.assertFalse(self.compile.exception(), self.mkFailMsg())
        self.checkPassed()

    def mkCustomMsg(self, msg):
        return f"""
### Command: {" ".join(self.compile.argv)}
### {msg}
### stderr:
{self.compile.stderr}"""

    def mkFailMsg(self):
        return f"""
### Command: {" ".join(self.compile.argv)}
### stderr:
{self.compile.stderr}"""

    def shortDescription(self):
        return f'{self.__class__.__name__} test of "{self.filename}"'


class OkTestCase(IPDLTestCase):
    """An invocation of the IPDL compiler on a valid specification.
    The IPDL compiler should not produce errors or exceptions.
    Lines of the form "//expect: <generated file>: <text>" require <text>
    to appear in that generated file."""

    def __init__(self, ipdlargv, filename):
        IPDLTestCase.__init__(self, ipdlargv, filename)

        self.expectedOutput = []
        with open(filename) as f:
            for l in f:
                if l.startswith("//expect:"):
                    outfile, text = l[len("//expect:") :].split(":", 1)
                    self.expectedOutput.append((outfile.strip(), text.strip()))

    def checkPassed(self):
        self.assertTrue(self.compile.ok(), self.mkFailMsg())

        for outfile, text in self.expectedOutput:
            self.assertIn(
                outfile,
                self.compile.outputs,
                self.mkCustomMsg('Did not generate "' + outfile + '"'),
            )
            self.assertIn(
                text,
                self.compile.outputs[outfile],
                self.mkCustomMsg('Did not see "' + text + '" in "' + outfile + '"'),
            )


class ErrorTestCase(IPDLTestCase):
    """An invocation of the IPDL compiler on an *invalid* specification.
    The IPDL compiler *should* produce errors but not exceptions."""

    def __init__(self, ipdlargv, filename):
        IPDLTestCase.__init__(self, ipdlargv, filename)

        # Look for expected errors in the input file.
        f = open(filename)
        self.expectedErrorMessage = []
        for l in f:
            if l.startswith("//error:"):
                self.expectedErrorMessage.append(l[2:-1])
        f.close()

    def checkPassed(self):
        self.assertNotEqual(
            self.expectedErrorMessage,
            [],
            self.mkCustomMsg(
                "Error test should contain at least "
                + "one line starting with //error: "
                + "that indicates the expected failure."
            ),
        )

        for e in self.expectedErrorMessage:
            self.assertTrue(
                self.compile.error(e),
                self.mkCustomMsg('Did not see expected error "' + e + '"'),
            )


if __name__ == "__main__":
    import sys

    okdir = sys.argv[1]
    assert os.path.isdir(okdir)
    errordir = sys.argv[2]
    assert os.path.isdir(errordir)

    ipdlargv = []
    oksuite = unittest.TestSuite()
    errorsuite = unittest.TestSuite()

    oktests, errortests = False, False
    for arg in sys.argv[3:]:
        if errortests:
            # The extra subdirectory is used for non-failing files we want
            # to include from failing files.
            errorIncludes = ["-I", os.path.join(errordir, "extra"), "-I", errordir]
            for test in glob.glob(arg):
                errorsuite.addTest(ErrorTestCase(ipdlargv + errorIncludes, test))
        elif oktests:
            if "--error-tests" == arg:
                errortests = True
                continue
            for test in glob.glob(arg):
                oksuite.addTest(OkTestCase(ipdlargv + ["-I", okdir], test))
        else:
            if "--ok-tests" == arg:
                oktests = True
                continue
            ipdlargv.append(arg)

    test_result = (unittest.TextTestRunner()).run(
        unittest.TestSuite([oksuite, errorsuite])
    )
    sys.exit(not test_result.wasSuccessful())
