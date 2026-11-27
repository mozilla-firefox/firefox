/**
 * Provenance for tests found by mutation testing.
 *
 * Each test was found by deliberately injecting a small change (a "mutant") into one component's
 * source (e.g. the Mesa NIR compiler) and searching for a shader whose result differs between the
 * mutated and unmutated builds. The rest of the CTS did not detect these mutants, which means that
 * the tests check behaviour that existing tests did not.
 */

/** A component of the WebGPU stack used to run the test, e.g. Dawn or Mesa. */
export interface ComponentVersion {
  component: string;
  /** Git commit in the component's repository. */
  version: string;
}

/** The configuration in which a test detected a mutant. */
export interface MutationInfo {
  /** The mutated component, at the commit the mutation was applied to. */
  mutant: ComponentVersion & {
    /** Location of the mutation in the component's source. */
    file: string;
    line: number;
    /** Set if `file` is generated during the build: the script that generates it. */
    generatedFrom?: string;
  };
  /** Other components used unmodified to run the test (e.g. Dawn). */
  environment: ComponentVersion[];
}
