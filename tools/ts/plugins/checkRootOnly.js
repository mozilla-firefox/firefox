/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/*
 * TypeScript transformer to automatically enable @ts-check for root files,
 * only those directly referenced from the tsconfig.json include option,
 * and automatically disable for everything else.
 */
exports.default = (program, host, _, { ts }) => {
  return ts.createProgram(
    program.getRootFileNames(),
    program.getCompilerOptions(),
    {
      ...host,
      getSourceFile(...args) {
        let file = host.getSourceFile(...args);
        // If the source file doesn't have a @ts-nocheck directive.
        if (file.checkJsDirective?.enabled !== false) {
          // Add one based on it being included as root file in tsconfig.
          file.checkJsDirective = {
            enabled: program.getRootFileNames().includes(file.fileName),
          };
        }
        return file;
      },
    },
    undefined,
    program.getConfigFileParsingDiagnostics()
  );
};
