# This is a sample Python script.

# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.

import argparse
import functools
import shutil

import jinja2
import json
import hashlib
import os
import random
import stat
import subprocess
import sys
import time

import prepare_csmith_program

from enum import auto, Enum
from pathlib import Path
from typing import Dict, List, Set


class MutationTreeNode:
    def __init__(self, mutation_ids, children):
        self.children = children
        self.mutation_ids = mutation_ids


def get_mutation_ids_for_mutation_group(mutation_group):
    if "replaceExpr" in mutation_group:
        return [instance["mutationId"] for instance in mutation_group["replaceExpr"]["instances"]]
    if "replaceBinaryOperator" in mutation_group:
        return [instance["mutationId"] for instance in mutation_group["replaceBinaryOperator"]["instances"]]
    if "replaceUnaryOperator" in mutation_group:
        return [instance["mutationId"] for instance in mutation_group["replaceUnaryOperator"]["instances"]]
    assert "removeStmt" in mutation_group
    return [mutation_group["removeStmt"]["mutationId"]]


def get_mutation_ids_for_json_node(node):
    assert "mutationGroups" in node
    return functools.reduce(lambda x, y: x + y, map(get_mutation_ids_for_mutation_group, node["mutationGroups"]), [])


class MutationTree:
    def __init__(self, json_data):

        def populate(json_node, node_id):
            children = []
            for child_json_node in json_node["children"]:
                child_node_id = self.num_nodes
                children.append(child_node_id)
                self.parent_map[child_node_id] = node_id
                self.num_nodes += 1
                populate(child_json_node, child_node_id)
            self.nodes[node_id] = MutationTreeNode(get_mutation_ids_for_json_node(json_node), children)
            self.num_mutations = max(self.num_mutations, functools.reduce(max, self.nodes[node_id].mutation_ids, 0))
            for mutation_id in self.nodes[node_id].mutation_ids:
                self.mutation_id_to_node_id[mutation_id] = node_id

        self.nodes = {}
        self.parent_map = {}
        self.mutation_id_to_node_id = {}
        self.num_mutations = 0
        self.num_nodes = 0

        for root_json_node in [file["mutationTreeRoot"] for file in json_data["infoForFiles"]]:
            root_node_id = self.num_nodes
            self.num_nodes += 1
            populate(root_json_node, root_node_id)

    def get_mutation_ids_for_subtree(self, node_id) -> List[int]:
        assert 0 <= node_id < self.num_nodes
        return self.nodes[node_id].mutation_ids + functools.reduce(lambda x, y: x + y,
                                                                   map(lambda x: self.get_mutation_ids_for_subtree(x),
                                                                       self.nodes[node_id].children), [])

    def get_incompatible_mutation_ids(self, mutation_id) -> List[int]:
        assert 0 <= mutation_id < self.num_mutations
        node_id = self.mutation_id_to_node_id[mutation_id]
        result = self.get_mutation_ids_for_subtree(node_id)
        while node_id in self.parent_map:
            node_id = self.parent_map[node_id]
            result += self.nodes[node_id].mutation_ids
        return result


class GeneratedProgramStats:
    def __init__(self, compile_time: float, execution_time: float, expected_output: str,
                 executable_hash: str):
        self.compile_time = compile_time
        self.execution_time = execution_time
        self.expected_output = expected_output
        self.executable_hash = executable_hash


def hash_file(filename: str) -> str:
    md5_hash = hashlib.md5()
    md5_hash.update(open(filename, 'rb').read())
    return md5_hash.hexdigest()


class ExecutionStatus(Enum):
    NO_EFFECT = auto()
    DIFFERENT_BINARIES_SAME_RESULT = auto()
    COMPILE_FAIL_KILL = auto()
    COMPILE_TIMEOUT_KILL = auto()
    RUN_FAIL_KILL = auto()
    RUN_TIMEOUT_KILL = auto()
    MISCOMPILATION_KILL = auto()


class MutantKiller:
    def __init__(self,
                 mutation_tree: MutationTree,
                 csmith_root: Path,
                 compiler_executable: Path,
                 max_attempts_per_program: int,
                 num_simultaneous_mutations: int,
                 max_consecutive_failed_attempts_per_program: int):
        self.mutation_tree: MutationTree = mutation_tree
        self.csmith_root: Path = csmith_root
        self.compiler_executable: Path = compiler_executable
        self.max_attempts_per_program: int = max_attempts_per_program
        self.num_simultaneous_mutations: int = num_simultaneous_mutations
        self.max_consecutive_failed_attempts_per_program: int = max_consecutive_failed_attempts_per_program
        # Maps each unkilled mutant to the number of times it has been failed to be killed.
        self.unkilled_mutants: Dict[int, int] = {mutant: 0 for mutant in range(0, self.mutation_tree.num_mutations)}
        self.killed_mutants: Dict[int, ExecutionStatus] = {}
        self.round = 0

    def generate_interestingness_test(self,
                                      mutant: int) -> None:
        template_loader = jinja2.FileSystemLoader(searchpath="./")
        template_env = jinja2.Environment(loader=template_loader)
        template = template_env.get_template("interesting.py.template")
        open('__interesting.py', 'w').write(template.render(csmith_root=self.csmith_root,
                                                            compiler_executable=self.compiler_executable,
                                                            mutation_ids=str(mutant)))
        # Make the interestingness test executable.
        st = os.stat('__interesting.py')
        os.chmod('__interesting.py', st.st_mode | stat.S_IEXEC)

    def reduce_very_strong_kill(self,
                                mutant: int) -> bool:
        self.generate_interestingness_test(mutant=mutant)
        creduce_environment = os.environ.copy()
        creduce_environment['CREDUCE_INCLUDE_PATH'] = \
            str(self.csmith_root / 'runtime') + ':' + str(self.csmith_root / 'build' / 'runtime')
        creduce_result: subprocess.CompletedProcess = subprocess.run(['creduce', '__interesting.py', '__prog.c'],
                                                                     env=creduce_environment)
        if creduce_result.returncode != 0:
            print('creduce failed')
            return False
        return True

    def is_killed_by_reduced_test_case(self, mutant: int) -> bool:
        self.generate_interestingness_test(mutant=mutant)
        if subprocess.run(['./__interesting.py']).returncode == 0:
            print(f'Reduced file kills mutant {mutant}')
            return True
        print(f'Reduced file does not kill mutant {mutant}')
        return False

    def try_to_kill_mutants(self,
                            program_stats: GeneratedProgramStats,
                            selected_mutants: List[int]) -> ExecutionStatus:
        try:
            cmd: List[str] = [self.compiler_executable, "-O3", "-I", self.csmith_root / "runtime", "-I",
                              self.csmith_root / "build" / "runtime", "__prog.c", "-o", "__prog_mutated"]
            mutated_environment: Dict[str, str] = os.environ.copy()
            mutated_environment['DREDD_ENABLED_MUTATION'] = ",".join([str(m) for m in selected_mutants])
            result = subprocess.run(cmd, timeout=max(5.0, 5.0 * program_stats.compile_time), capture_output=True,
                                    env=mutated_environment)
        except subprocess.TimeoutExpired:
            print("WEAK KILL: Compilation with mutated compiler timed out.")
            sys.stdout.flush()
            return ExecutionStatus.COMPILE_TIMEOUT_KILL

        if result.returncode != 0:
            print("WEAK KILL: Compilation with mutated compiler failed.")
            sys.stdout.flush()
            return ExecutionStatus.COMPILE_FAIL_KILL

        print("Compilation with mutated compiler succeeded")
        sys.stdout.flush()
        if hash_file('__prog_mutated') == program_stats.executable_hash:
            print("Binaries are the same - not interesting")
            sys.stdout.flush()
            return ExecutionStatus.NO_EFFECT

        print("Different binaries!")
        sys.stdout.flush()
        try:
            cmd: List[str] = ["./__prog_mutated"]
            result = subprocess.run(cmd, timeout=max(5.0, 10.0 * program_stats.execution_time), capture_output=True)
            if result.returncode != 0:
                print("STRONG KILL: Execution of program compiled with mutated compiler failed.")
                sys.stdout.flush()
                return ExecutionStatus.RUN_FAIL_KILL
            if result.stdout.decode('utf-8') != program_stats.expected_output:
                print("VERY STRONG KILL: Execution results from program compiled with mutated compiler are different!")
                sys.stdout.flush()
                return ExecutionStatus.NO_EFFECT
                #return ExecutionStatus.MISCOMPILATION_KILL

        except subprocess.TimeoutExpired:
            print("STRONG KILL: Execution of program compiled with mutated compiler timed out.")
            sys.stdout.flush()
            return ExecutionStatus.RUN_TIMEOUT_KILL

        print("Same execution results - not interesting")
        sys.stdout.flush()
        return ExecutionStatus.DIFFERENT_BINARIES_SAME_RESULT

    def consolodate_kill(self,
                         mutant: int,
                         execution_status: ExecutionStatus,
                         program_stats: GeneratedProgramStats) -> None:
        relatives: List[int] = self.mutation_tree.get_incompatible_mutation_ids(mutant)
        print(f"Consolodating kills by trying {len(relatives) - 1} related mutants")
        miscompilation_kills_to_reduce: List[int] = []
        if execution_status == ExecutionStatus.MISCOMPILATION_KILL:
            miscompilation_kills_to_reduce.append(mutant)

        follow_on_kills: int = 0
        for relative in relatives:
            if relative == mutant or relative in self.killed_mutants:
                continue

            result_for_relative: ExecutionStatus = self.try_to_kill_mutants(program_stats=program_stats,
                                                                            selected_mutants=[relative])
            if result_for_relative == ExecutionStatus.NO_EFFECT or result_for_relative \
                    == ExecutionStatus.DIFFERENT_BINARIES_SAME_RESULT:
                self.unkilled_mutants[relative] += 1
                continue
            self.unkilled_mutants.pop(relative)
            self.killed_mutants[relative] = result_for_relative
            follow_on_kills += 1
            if result_for_relative == ExecutionStatus.MISCOMPILATION_KILL:
                miscompilation_kills_to_reduce.append(relative)

        print(f"Found {follow_on_kills} follow-on kills, {len(miscompilation_kills_to_reduce)} of which are miscompilations")
        sys.stdout.flush()
        while len(miscompilation_kills_to_reduce) > 0:
            mutant: int = miscompilation_kills_to_reduce.pop()
            if not self.reduce_very_strong_kill(mutant=mutant):
                # This should never really happen: the mutant was previously confirmed as being miscompilation-killed,
                # thus it should successfully reduce. It could happen due to nondeterminism.
                continue
            killed_by_reduced_test_case: List[int] = [mutant]
            index: int = 0
            while index < len(miscompilation_kills_to_reduce):
                follow_on_mutant: int = miscompilation_kills_to_reduce[index]
                if self.is_killed_by_reduced_test_case(mutant=follow_on_mutant):
                    killed_by_reduced_test_case.append(follow_on_mutant)
                    miscompilation_kills_to_reduce.pop(index)
                else:
                    index += 1
            print(f"Found {len(killed_by_reduced_test_case)} miscompilation kills from a reduced test case")
            shutil.move(src='__prog.c', dst=f'__kills_{"_".join([str(m) for m in killed_by_reduced_test_case])}.c')

    def search_for_kills_in_mutant_selection(self,
                                             program_stats: GeneratedProgramStats,
                                             selected_mutants: List[int]) -> bool:
        assert len(selected_mutants) > 0
        print(f"Searching for kills among {len(selected_mutants)} mutant(s)")
        execution_status: ExecutionStatus = self.try_to_kill_mutants(program_stats=program_stats,
                                                                     selected_mutants=selected_mutants)
        sys.stdout.flush()
        if execution_status == ExecutionStatus.NO_EFFECT or \
                execution_status == ExecutionStatus.DIFFERENT_BINARIES_SAME_RESULT:
            for m in selected_mutants:
                self.unkilled_mutants[m] += 1
            return False

        if len(selected_mutants) == 1:
            self.unkilled_mutants.pop(selected_mutants[0])
            self.killed_mutants[selected_mutants[0]] = execution_status
            self.consolodate_kill(mutant=selected_mutants[0],
                                  execution_status=execution_status,
                                  program_stats=program_stats)
            return True

        midpoint: int = int(len(selected_mutants) / 2)
        selected_mutants_lhs: List[int] = selected_mutants[0:midpoint]
        result: bool = self.search_for_kills_in_mutant_selection(program_stats=program_stats,
                                                                 selected_mutants=selected_mutants_lhs)
        selected_mutants_rhs = [m for m in selected_mutants[midpoint:] if m not in self.killed_mutants]
        result = result or self.search_for_kills_in_mutant_selection(program_stats=program_stats,
                                                                     selected_mutants=selected_mutants_rhs)
        print(f"Finished searching for kills among {len(selected_mutants)} mutant(s)")
        return result

    def select_mutants(self) -> List[int]:
        available_mutants: Set[int] = {m for m in self.unkilled_mutants if self.unkilled_mutants[m] == self.round}
        if len(available_mutants) == 0:
            self.round += 1
            print(f"Moving to round {self.round}")
            available_mutants = {m for m in self.unkilled_mutants if self.unkilled_mutants[m] == self.round}
        result: List[int] = []
        while len(available_mutants) > 0 and len(result) < self.num_simultaneous_mutations:
            selected_mutant = list(available_mutants)[random.randrange(0, len(available_mutants))]
            result.append(selected_mutant)
            available_mutants = available_mutants.difference(
                set(self.mutation_tree.get_incompatible_mutation_ids(selected_mutant)))
        return result

    def generate_a_program(self) -> GeneratedProgramStats:
        while True:
            # TODO: cleanup
            try:
                # Run csmith until it yields a program
                cmd: List[str] = [self.csmith_root / "build" / "src" / "csmith", "-o", "__prog.c"]
                result = subprocess.run(cmd, timeout=10, capture_output=True)
                if result.returncode != 0:
                    print("csmith failed")
                    continue
            except subprocess.TimeoutExpired:
                print("csmith timed out")
                continue

            # Inline some immediate header files into the Csmith-generated program
            prepare_csmith_program.prepare(Path("__prog.c"), Path("__prog.c"), self.csmith_root)

            try:
                cmd: List[str] = [self.compiler_executable,
                                  "-O3",
                                  "-I",
                                  self.csmith_root / "runtime",
                                  "-I",
                                  self.csmith_root / "build" / "runtime",
                                  "__prog.c",
                                  "-o",
                                  "__prog"]
                compile_time_start: float = time.time()
                result = subprocess.run(cmd, timeout=10, capture_output=True)
                compile_time_end: float = time.time()
                if result.returncode != 0:
                    print("compilation with non-mutated compiler failed")
                    continue
            except subprocess.TimeoutExpired:
                print("compilation with non-mutated compiler timed out")
                continue

            try:
                cmd: List[str] = ["./__prog"]
                execute_time_start: float = time.time()
                result = subprocess.run(cmd, timeout=10, capture_output=True)
                execute_time_end: float = time.time()
                if result.returncode != 0:
                    print("execution of generated program compiled with non-mutated compiler failed")
                    continue
            except subprocess.TimeoutExpired:
                print("execution of generated program compiled with non-mutated compiler timed out")
                continue

            return GeneratedProgramStats(compile_time=compile_time_end - compile_time_start,
                                         execution_time=execute_time_end - execute_time_start,
                                         expected_output=result.stdout.decode('utf-8'),
                                         executable_hash=hash_file('__prog'))

    def go(self):
        while True:
            print("Generating a program...")
            program_stats: GeneratedProgramStats = self.generate_a_program()
            print("Generated!")
            num_attempts_for_current_program: int = 0
            num_consecutive_failed_attempts_for_current_program: int = 0
            while num_attempts_for_current_program < self.max_attempts_per_program and \
                    num_consecutive_failed_attempts_for_current_program < \
                    self.max_consecutive_failed_attempts_per_program:
                print("Trying a kill attempt")
                print(f"Kill attempts for this program so far: {num_attempts_for_current_program}")
                print("Consecutive failed kill attempts for this program: "
                      f"{num_consecutive_failed_attempts_for_current_program}")
                print(f"Total killed mutants: {len(self.killed_mutants)}")
                print(f"Total remaining mutants: {len(self.unkilled_mutants)}")
                print(f"Mutants still to be tried during round {self.round}: {len({m for m in self.unkilled_mutants if self.unkilled_mutants[m] == self.round}) }")
                sys.stdout.flush()
                if self.search_for_kills_in_mutant_selection(selected_mutants=self.select_mutants(),
                                                             program_stats=program_stats):
                    num_consecutive_failed_attempts_for_current_program = 0
                else:
                    num_consecutive_failed_attempts_for_current_program += 1
                assert len(self.killed_mutants) + len(self.unkilled_mutants) == self.mutation_tree.num_mutations
                num_attempts_for_current_program += 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mutation_info_file",
                        help="File containing information about mutations.",
                        type=Path)
    parser.add_argument("compiler_executable",
                        help="Path to the executable for the Dredd-mutated compiler.",
                        type=Path)
    parser.add_argument("csmith_root", help="Path to a checkout of Csmith, assuming that it has been built under "
                                            "'build' beneath this directory.",
                        type=Path)
    parser.add_argument("--max_consecutive_failed_attempts_per_program",
                        default=10,
                        help="The number of times a given generated program will be compiled and executed in a row "
                             "with no mutation being killed.",
                        type=int)
    parser.add_argument("--max_attempts_per_program",
                        default=100,
                        help="The number of times a given program will be compiled and executed to look for kills ("
                             "excluding work done investigating kills) before moving on to the next program.",
                        type=int)
    parser.add_argument("--num_simultaneous_mutations",
                        default=64,
                        help="The number of mutations that will be enabled simultaneously when looking for kills.",
                        type=int)
    args = parser.parse_args()

    print("Building the mutation tree...")
    with open(args.mutation_info_file, 'r') as json_input:
        mutation_tree = MutationTree(json.load(json_input))
    print("Built!")

    mutant_killer: MutantKiller = MutantKiller(mutation_tree=mutation_tree,
                                               csmith_root=args.csmith_root,
                                               compiler_executable=args.compiler_executable,
                                               max_attempts_per_program=args.max_attempts_per_program,
                                               max_consecutive_failed_attempts_per_program=args.max_consecutive_failed_attempts_per_program,
                                               num_simultaneous_mutations=args.num_simultaneous_mutations)
    mutant_killer.go()


if __name__ == '__main__':
    main()
