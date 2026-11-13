# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# the following is not required in 3.11+
from __future__ import annotations

# ruff linter deprecates List required for Python 3.8 compatibility
from typing import List, Optional, Tuple  # noqa UP035

ListStr = List[str]  # noqa UP006

AND: str = "&&"
EQUAL: str = "=="
NOT_EQUAL: str = "!="
GREATER_EQUAL: str = ">="


def strip_unquoted(s: str) -> str:
    """
    Remove leading and trailing spaces
    Collapse multiple spaces to one inside the string
    Except do not alter quoted parts
    """
    result = ""
    quoting: bool = False
    prev: str = ""
    for ch in s.strip():
        if quoting:
            if ch in ['"', "'"]:
                quoting = False
        elif ch in ['"', "'"]:
            quoting = True
        if quoting or ch != " " or prev != " ":
            result += ch
        prev = ch
    return result


class TokenType:
    """Helper class to identify manifest condition token types"""

    def __init__(self, **kwargs):
        self._boolean: bool = bool(kwargs.get("boolean", False))
        self._name: str = str(kwargs.get("name", ""))
        self._rank: int = int(kwargs.get("rank", 0))

    def __repr__(self) -> str:
        return (
            f"<TokenType '{self.name()}' rank {self.rank()} boolean '{self.boolean()}'>"
        )

    def boolean(self) -> bool:
        return self._boolean

    def name(self) -> str:
        return self._name

    def rank(self) -> int:
        return self._rank


class Token:
    """Helper class to identify legal manifest variables and values"""

    def __init__(self, **kwargs):
        self.t_type: TokenType = kwargs.get("t_type", None)
        self._depends: Optional[Token] = kwargs.get("depends", None)  # noqa UP035
        self._value: str = str(kwargs.get("value", ""))

    def __str__(self) -> str:
        return f"{self.t_type.name()} == '{self._value}'"

    def __repr__(self) -> str:
        if self._depends is not None:
            return f"<Token '{self._value}' is type '{self.t_type.name()} depends on {self._depends}'>"
        return f"<Token '{self._value}' is type '{self.t_type.name()}'>"

    def depends(self) -> Optional[Token]:  # noqa UP035
        return self._depends

    def type(self) -> TokenType:
        return self.t_type

    def value(self) -> str:
        return self._value


OptTokenType = Optional[TokenType]  # noqa UP006
OptStr = Optional[str]  # noqa UP006
TokenizedOps = List[Tuple[OptTokenType, OptStr, Token]]  # noqa UP006


class ManifestTokens:
    """Set of all valid manifest tokens"""

    def __init__(self):
        self.rank: List[TokenType] = []  # noqa UP035
        self.tokens = {}

        # RANKED VARIABLES (left hand side)
        t_cc_type: TokenType = self.add_t_token(
            TokenType(name="cc_type", rank=len(self.rank))
        )
        t_os: TokenType = self.add_t_token(
            TokenType(name="os", rank=len(self.rank), boolean=False)
        )
        t_os_version: TokenType = self.add_t_token(
            TokenType(name="os_version", rank=len(self.rank))
        )
        t_arch: TokenType = self.add_t_token(
            TokenType(name="arch", rank=len(self.rank))
        )
        t_display: TokenType = self.add_t_token(
            TokenType(name="display", rank=len(self.rank))
        )
        t_buildapp: TokenType = self.add_t_token(
            TokenType(name="buildapp", rank=len(self.rank))
        )
        t_appname: TokenType = self.add_t_token(
            TokenType(name="appname", rank=len(self.rank))
        )
        t_build_type: TokenType = self.add_t_token(
            TokenType(name="build_type", rank=len(self.rank), boolean=True)
        )
        t_variant: TokenType = self.add_t_token(
            TokenType(name="variant", rank=len(self.rank), boolean=True)
        )
        t_other_flags: TokenType = self.add_t_token(
            TokenType(name="other_flags", rank=len(self.rank), boolean=True)
        )

        # legal VALUES for variables (right hand side)
        _clang: Token = self.add(Token(t_type=t_cc_type, value="clang"))

        android: Token = self.add(Token(t_type=t_os, value="android"))
        linux: Token = self.add(Token(t_type=t_os, value="linux"))
        mac: Token = self.add(Token(t_type=t_os, value="mac"))
        win: Token = self.add(Token(t_type=t_os, value="win"))

        _v14: Token = self.add(Token(t_type=t_os_version, depends=android, value="14"))

        _v2204: Token = self.add(
            Token(t_type=t_os_version, depends=linux, value="22.04")
        )
        _v2404: Token = self.add(
            Token(t_type=t_os_version, depends=linux, value="24.04")
        )
        _v2604: Token = self.add(
            Token(t_type=t_os_version, depends=linux, value="26.04")
        )

        _v1015: Token = self.add(Token(t_type=t_os_version, depends=mac, value="10.15"))
        _v1470: Token = self.add(Token(t_type=t_os_version, depends=mac, value="14.70"))
        _v1530: Token = self.add(Token(t_type=t_os_version, depends=mac, value="15.30"))

        _v102009: Token = self.add(
            Token(t_type=t_os_version, depends=win, value="10.2009")
        )
        _v1126100: Token = self.add(
            Token(t_type=t_os_version, depends=win, value="11.26100")
        )
        _v1126200: Token = self.add(
            Token(t_type=t_os_version, depends=win, value="11.26200")
        )

        _aarch64: Token = self.add(Token(t_type=t_arch, value="aarch64"))
        _armeabi_v7a: Token = self.add(
            Token(t_type=t_arch, value="armeabi-v7a")
        )  # deprecated
        _x86: Token = self.add(Token(t_type=t_arch, value="x86"))
        _x86_64: Token = self.add(Token(t_type=t_arch, value="x86_64"))

        _x11: Token = self.add(Token(t_type=t_display, depends=linux, value="x11"))
        _wayland: Token = self.add(
            Token(t_type=t_display, depends=linux, value="wayland")
        )

        _browser: Token = self.add(Token(t_type=t_buildapp, value="browser"))

        _firefox: Token = self.add(Token(t_type=t_appname, value="firefox"))
        _seamonkey: Token = self.add(Token(t_type=t_appname, value="seamonkey"))
        _thunderbird: Token = self.add(Token(t_type=t_appname, value="thunderbird"))

        # build_type flags
        _asan: Token = self.add(Token(t_type=t_build_type, value="asan"))
        _ccov: Token = self.add(Token(t_type=t_build_type, value="ccov"))
        _debug: Token = self.add(Token(t_type=t_build_type, value="debug"))
        _tsan: Token = self.add(Token(t_type=t_build_type, value="tsan"))
        _opt: Token = self.add(Token(t_type=t_build_type, value="opt"))

        # variants
        # grep mozinfo taskcluster/test_configs/variants.yml | cut -d\" -f2 | sort -u
        _a11y_checks: Token = self.add(Token(t_type=t_variant, value="a11y_checks"))
        _condprof: Token = self.add(Token(t_type=t_variant, value="condprof"))
        _e10s: Token = self.add(Token(t_type=t_variant, value="e10s"))
        _emesigned: Token = self.add(Token(t_type=t_variant, value="emesigned"))
        _emewmf: Token = self.add(Token(t_type=t_variant, value="emewmf"))
        _fission: Token = self.add(Token(t_type=t_variant, value="fission"))
        # fission-high-value -- not used in any manifests
        _headless: Token = self.add(Token(t_type=t_variant, value="headless"))
        _http2: Token = self.add(Token(t_type=t_variant, value="http2"))
        _http3: Token = self.add(Token(t_type=t_variant, value="http3"))
        _inc_origin_init: Token = self.add(
            Token(t_type=t_variant, value="inc_origin_init")
        )
        _mda_gpu: Token = self.add(Token(t_type=t_variant, value="mda_gpu"))
        _msix: Token = self.add(Token(t_type=t_variant, value="msix"))
        _nogpu: Token = self.add(Token(t_type=t_variant, value="nogpu"))
        _nova: Token = self.add(Token(t_type=t_variant, value="nova"))
        _private_browsing: Token = self.add(
            Token(t_type=t_variant, value="privateBrowsing")
        )
        _remote_async: Token = self.add(Token(t_type=t_variant, value="remote_async"))
        _snapshot: Token = self.add(Token(t_type=t_variant, value="snapshot"))
        _standalone: Token = self.add(Token(t_type=t_variant, value="standalone"))
        _swgl: Token = self.add(Token(t_type=t_variant, value="swgl"))
        _trainhop: Token = self.add(Token(t_type=t_variant, value="trainhop"))
        _vertical_tab: Token = self.add(Token(t_type=t_variant, value="vertical_tab"))
        _wmfme: Token = self.add(Token(t_type=t_variant, value="wmfme"))
        _xorigin: Token = self.add(Token(t_type=t_variant, value="xorigin"))

        # other flags
        _artifact: Token = self.add(Token(t_type=t_other_flags, value="artifact"))
        _crashreporter: Token = self.add(
            Token(t_type=t_other_flags, value="crashreporter")
        )
        _datareporting: Token = self.add(
            Token(t_type=t_other_flags, value="datareporting")
        )
        _devedition: Token = self.add(Token(t_type=t_other_flags, value="devedition"))
        _early_beta_or_earlier: Token = self.add(
            Token(t_type=t_other_flags, value="early_beta_or_earlier")
        )
        _enterprise: Token = self.add(Token(t_type=t_other_flags, value="enterprise"))
        _false: Token = self.add(Token(t_type=t_other_flags, value="false"))
        _gecko_profiler: Token = self.add(
            Token(t_type=t_other_flags, value="gecko_profiler")
        )
        _isolated_process: Token = self.add(
            Token(t_type=t_other_flags, value="isolated_process")
        )
        _is_emulator: Token = self.add(Token(t_type=t_other_flags, value="is_emulator"))
        _is_ubuntu: Token = self.add(
            Token(t_type=t_other_flags, value="is_ubuntu")
        )  # deprecated
        _macos_vm: Token = self.add(Token(t_type=t_other_flags, value="macos_vm"))
        _msix: Token = self.add(Token(t_type=t_other_flags, value="msix"))
        _nightly_build: Token = self.add(
            Token(t_type=t_other_flags, value="nightly_build")
        )
        _release_or_beta: Token = self.add(
            Token(t_type=t_other_flags, value="release_or_beta")
        )
        _require_signing: Token = self.add(
            Token(t_type=t_other_flags, value="require_signing")
        )
        _sessionHistoryInParent: Token = self.add(
            Token(t_type=t_other_flags, value="sessionHistoryInParent")
        )
        _sync: Token = self.add(Token(t_type=t_other_flags, value="sync"))
        _true: Token = self.add(Token(t_type=t_other_flags, value="true"))
        _updater: Token = self.add(Token(t_type=t_other_flags, value="updater"))
        _verify: Token = self.add(Token(t_type=t_other_flags, value="verify"))
        _verify_standalone: Token = self.add(
            Token(t_type=t_other_flags, value="verify-standalone")
        )

    def add_t_token(self, t_type: TokenType) -> TokenType:
        self.tokens[t_type.name()] = t_type
        self.rank.append(t_type)
        return t_type

    def add(self, token: Token) -> Token:
        self.tokens[token.value()] = token
        return token

    def canonical_condition(self, condition: str) -> str:
        """
        Examine condition to see if it is canonical
        and return the empty string if so, else error msg if not
        In well formed conditions...
        "var op value" where op in ==, != and >=
        "flag" "!flag"
        every var should be
        - found in tokens as a TokenType, boolean() == False
        - in rank order
        every value should be
        - found in tokens as a Token
        - token.type() should match var
        - if token.depends() then a value of that matches depends.type()
            should be found at a lower rank
        every flag should be
        - found in tokens as a Token, with token.type().boolean() == True
        """

        msg: str = ""
        operator: str = ""
        unquoted: str = strip_unquoted(condition)
        if unquoted.startswith("(") and unquoted.endswith(")"):
            msg = f"remove superfluous expression parenthesis: {unquoted}"
            return msg
        ops: ListStr = unquoted.split(AND)
        tops: TokenizedOps = []
        var: str = ""
        op: str = ""
        value: str = ""
        for i in range(len(ops)):
            op = ops[i].strip()
            j: int = op.find(" ")
            if op == "":
                msg = "empty condition"
                return msg
            elif j > 1:
                k: int = op.find(" ", j + 1)
                if k > 0:
                    var = op[0:j]
                    operator = op[j + 1 : k]
                    value = op[k + 1 :].strip("'\"")
                    if op.startswith("(") and op.endswith(")"):
                        continue  # currently do NOT lint subexpressions
                else:
                    msg = (
                        "expected each operation to be an single variable or comparison"
                    )
                    return msg
            elif op in self.tokens:
                token = self.tokens[op]
                tops.append((None, None, token))
                continue
            elif op.startswith("!") and op[1:] in self.tokens:
                token = self.tokens[op[1:]]
                tops.append((None, None, token))
                continue
            else:
                msg = f"unknown other flag: '{op}'"
                return msg
            if operator not in [EQUAL, NOT_EQUAL, GREATER_EQUAL]:
                msg = f"unknown comparison operator '{operator}' in comparison: {op}"
                return msg
            if var in self.tokens:
                token_type = self.tokens[var]
            elif var.find("(") >= 0:  # parenthetical expressions not handled
                return msg
            else:
                msg = f"unknown var '{var}' in comparison: {op}"
                return msg
            if value in self.tokens:
                token = self.tokens[value]
            else:
                msg = f"unknown value '{value}' in comparison: {op}"
                return msg
            tops.append((token_type, operator, token))

        last_token: Optional[Token] = None  # noqa UP035
        last_var: str = ""
        for top in tops:
            token_type, operator, token = top
            if token_type is None:
                var = token.value()
            else:
                var = token.type().name()
            if (
                last_token is not None
                and token.type().rank() < last_token.type().rank()
            ):
                msg = f"variable {var} (rank {token.type().rank()}) should appear before variable {last_var} (rank {last_token.type().rank()}) in condition: {unquoted}"
                return msg
            last_token = token
            last_var = var
            if token.depends() is not None:
                found: bool = False
                for t in tops:
                    if t == top:
                        continue
                    _, operator, tok = t
                    if tok == token.depends() and operator == EQUAL:
                        found = True
                        break
                if not found:
                    msg = f"value {token.value()} depends on {token.depends()} in condition: {unquoted}"
                    return msg
        return msg
