/**
 * Types specific to toolkit/extensions code.
 */
declare global {

  type BaseContext = import("ExtensionCommon.sys.mjs").BaseContext;
  type ExtensionChild = import("ExtensionChild.sys.mjs").ExtensionChild;
  type Extension = import("Extension.sys.mjs").Extension;
  type callback = (...any) => any;

  interface nsIDOMProcessChild  {
    getActor(name: "ProcessConduits"): ProcessConduitsChild;
  }

  interface WebExtensionContentScript {
    userScriptOptions: { scriptMetadata: object };
  }

  interface WebExtensionPolicy {
    extension: Extension;
    debugName: string;
    instanceId: string;
    optionalPermissions: string[];
  }

  // Can't define a const generic parameter in jsdocs yet.
  // https://github.com/microsoft/TypeScript/issues/56634
  function ConduitGen<const Send>(_, init: Init<Send>, _actor?): Conduit<Send>;
  type Items<A> = A extends ReadonlyArray<infer U extends string> ? U : never;
}

import { PointConduit, ProcessConduitsChild } from "ConduitsChild.sys.mjs";
import { ConduitAddress } from "ConduitsParent.sys.mjs";

type Conduit<Send> = PointConduit & { [s in `send${Items<Send>}`]: callback };
type Init<Send> = ConduitAddress & { send: Send; };
