# SPDX-License-Identifier: BSD-2-Clause
# Copyright (C) 2021 Intel Corporation.

import pytest
import re

from python_framework import CMD_helper


class Test_tiering(object):
    # NOTE: this script should be called from the root of memkind repository
    ld_preload_env = "LD_PRELOAD=tiering/.libs/libmemtier.so"
    # TODO create separate, parametrized binary that could be used for testing
    # instead of using ls here
    bin_path = "ls -l"
    cmd_helper = CMD_helper()

    re_hex_or_nil = r"((0[xX][a-fA-F0-9]+)|\(nil\))"
    re_log_debug_malloc = r"MEMKIND_MEM_TIERING_LOG_DEBUG: " + \
        r"malloc\(\d+\) = " + re_hex_or_nil
    re_log_debug_realloc = r"MEMKIND_MEM_TIERING_LOG_DEBUG: realloc\(" + \
        re_hex_or_nil + r", \d+\) = " + re_hex_or_nil
    re_log_debug_calloc = r"MEMKIND_MEM_TIERING_LOG_DEBUG: " + \
        r"calloc\(\d+, \d+\) = " + re_hex_or_nil
    re_log_debug_memalign = r"MEMKIND_MEM_TIERING_LOG_DEBUG: " + \
        r"memalign\(\d+, \d +\) = " + re_hex_or_nil
    re_log_debug_free = r"MEMKIND_MEM_TIERING_LOG_DEBUG: free\(" + \
        re_hex_or_nil + r"\)"

    def test_utils_init(self):
        log_level_env = "MEMKIND_MEM_TIERING_LOG_LEVEL=1"

        # run command without LD_PRELOAD
        default_output, default_retcode = self.cmd_helper.execute_cmd(
            self.bin_path)

        assert default_retcode == 0, "Test failed with error: \n" + \
            "Execution of: \'" + self.bin_path + "\' returns: " + \
            default_retcode + " \n" + "output: " + default_output

        # run command with LD_PRELOAD
        command = " ".join([self.ld_preload_env, log_level_env, self.bin_path])
        output, retcode = self.cmd_helper.execute_cmd(command)

        assert retcode == 0, "Test failed with error: \n" + \
            "Execution of: \'" + command + "\' returns: " + retcode + " \n" + \
            "output: " + output

        assert output.split("\n")[0] == "MEMKIND_MEM_TIERING_LOG_INFO: " + \
            "Memkind memtier lib loaded!", "Bad init message"

        # check if rest of output from command is unchanged
        rest_output = "\n".join(output.split("\n")[1:])
        assert rest_output == default_output, "Bad command output"

    def test_utils_log_level_default(self):
        # run command without LD_PRELOAD
        default_output, default_retcode = self.cmd_helper.execute_cmd(
            self.bin_path)

        assert default_retcode == 0, "Test failed with error: \n" + \
            "Execution of: \'" + self.bin_path + "\' returns: " + \
            default_retcode + " \n" + "output: " + default_output

        # run command with LD_PRELOAD
        log_level_env = "MEMKIND_MEM_TIERING_LOG_LEVEL=0"
        command = " ".join([self.ld_preload_env, log_level_env, self.bin_path])
        test_output, test_retcode = self.cmd_helper.execute_cmd(command)

        assert test_retcode == 0, "Test failed with error: \n" + \
            "Execution of: \'" + command + "\' returns: " + \
            test_retcode + " \n" + "output: " + test_output

        assert test_output == default_output, "Bad command output"

    def test_utils_log_level_not_set(self):
        # run command without LD_PRELOAD
        default_output, default_retcode = self.cmd_helper.execute_cmd(
            self.bin_path)

        assert default_retcode == 0, "Test failed with error: \n" + \
            "Execution of: \'" + self.bin_path + "\' returns: " + \
            default_retcode + " \n" + "output: " + default_output

        # run command with LD_PRELOAD
        command = " ".join([self.ld_preload_env, self.bin_path])
        test_output, test_retcode = self.cmd_helper.execute_cmd(command)

        assert test_retcode == 0, "Test failed with error: \n" + \
            "Execution of: \'" + command + "\' returns: " + \
            test_retcode + " \n" + "output: " + test_output

        assert test_output == default_output, "Bad command output"

    def test_utils_log_level_debug(self):
        # run command without LD_PRELOAD
        default_output, _ = self.cmd_helper.execute_cmd(self.bin_path)
        default_output = default_output.split("\n")

        # run command with LD_PRELOAD
        log_level_env = "MEMKIND_MEM_TIERING_LOG_LEVEL=2"
        command = " ".join([self.ld_preload_env, log_level_env, self.bin_path])
        output, _ = self.cmd_helper.execute_cmd(command)

        assert output.split("\n")[0] == "MEMKIND_MEM_TIERING_LOG_DEBUG: " + \
            "Setting log level to: 2", "Bad init message"

        assert output.split("\n")[1] == "MEMKIND_MEM_TIERING_LOG_INFO: " + \
            "Memkind memtier lib loaded!", "Bad init message"

        # next, extract from the output all lines starting with
        # "MEMKIND_MEM_TIERING_LOG" prefix and check if they are correct
        output = output.split("\n")[2:]
        log_output = [l for l in output
                      if l.startswith("MEMKIND_MEM_TIERING_LOG")]
        for log_line in log_output:
            is_malloc = re.match(self.re_log_debug_malloc, log_line)
            is_realloc = re.match(self.re_log_debug_realloc, log_line)
            is_calloc = re.match(self.re_log_debug_calloc, log_line)
            is_free = re.match(self.re_log_debug_free, log_line)
            assert is_malloc or is_realloc or is_calloc or is_free, \
                "Bad log message format: " + log_line

        # finally check if rest of output from command is unchanged
        output = [l for l in output if l not in log_output]
        assert output == default_output, "Bad ls output"

    def test_utils_log_level_negative(self):
        log_level_env = "MEMKIND_MEM_TIERING_LOG_LEVEL=-1"
        command = " ".join([self.ld_preload_env, log_level_env, self.bin_path])
        output_level_neg, _ = self.cmd_helper.execute_cmd(command)

        assert output_level_neg.split("\n")[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Wrong value of " + \
            "MEMKIND_MEM_TIERING_LOG_LEVEL=-1", "Bad init message"

    def test_utils_log_level_too_high(self):
        log_level_env = "MEMKIND_MEM_TIERING_LOG_LEVEL=4"
        command = " ".join([self.ld_preload_env, log_level_env, self.bin_path])
        output_level_neg, _ = self.cmd_helper.execute_cmd(command)

        assert output_level_neg.split("\n")[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Wrong value of " + \
            "MEMKIND_MEM_TIERING_LOG_LEVEL=4", "Bad init message"


class Test_tiering_config_env(object):
    ld_preload_env = "LD_PRELOAD=tiering/.libs/libmemtier.so"
    cmd_helper = CMD_helper()

    def get_cmd_output(self, config_env, log_level="0"):
        command = " ".join(
            [self.ld_preload_env, "MEMKIND_MEM_TIERING_LOG_LEVEL=" + log_level, config_env, "ls"])
        output, retcode = self.cmd_helper.execute_cmd(command)

        assert retcode == 0, \
            "Test failed with error: \nExecution of: '" + command + \
            "' returns: " + str(retcode) + "\noutput: " + output
        return output

    @pytest.mark.parametrize("ratio", ["1", "1000", "4294967295"])
    def test_DRAM(self, ratio):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=DRAM:" + ratio, log_level="2")

        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: kind_name: DRAM" in output, "Wrong message"
        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: ratio_value: " + ratio in output, \
            "Wrong message"

    @pytest.mark.parametrize("pmem_size", ["0", "1", "18446744073709551615"])
    def test_FSDAX(self, pmem_size):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=FS_DAX:/tmp/:" + pmem_size + ":1", log_level="2")

        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: kind_name: FS_DAX" in output, \
            "Wrong message"
        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: pmem_path: /tmp/" in output, \
            "Wrong message"
        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: pmem_size: " + pmem_size in output, \
            "Wrong message"
        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: ratio_value: 1" in output, \
            "Wrong message"

    @pytest.mark.parametrize("pmem_size", ["1073741824", "1048576K", "1024M", "1G"])
    def test_FSDAX_pmem_size_with_suffix(self, pmem_size):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=FS_DAX:/tmp/:" + pmem_size + ":1", log_level="2")

        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: kind_name: FS_DAX" in output, "Wrong message"
        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: pmem_path: /tmp/" in output, \
            "Wrong message"
        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: pmem_size: 1073741824" in output, \
            "Wrong message"
        assert "MEMKIND_MEM_TIERING_LOG_DEBUG: ratio_value: 1" in output, \
            "Wrong message"

    @pytest.mark.parametrize("pmem_size", ["-1", "-4294967295", "-18446744073709551615", "18446744073709551616"])
    def test_FSDAX_pmem_size_outside_limits(self, pmem_size):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=FS_DAX:/tmp/:" + pmem_size + ":1")

        assert output.splitlines()[0] == "MEMKIND_MEM_TIERING_LOG_ERROR: " + \
            "Failed to parse pmem size: " + pmem_size, "Wrong message"

    @pytest.mark.parametrize("pmem_size", ["18446744073709551615K", "18446744073709551615M", "18446744073709551615G"])
    def test_FSDAX_pmem_size_with_suffix_too_big(self, pmem_size):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=FS_DAX:/tmp/:" + pmem_size + ":1")
        assert "MEMKIND_MEM_TIERING_LOG_ERROR: Failed to parse pmem size: " + \
            pmem_size in output, "Wrong message"

        assert output.splitlines()[0] == "MEMKIND_MEM_TIERING_LOG_ERROR: " + \
            "Provided pmem size is too big: 18446744073709551615", \
            "Wrong message"

    def test_FSDAX_no_size(self):
        output = self.get_cmd_output("MEMKIND_MEM_TIERING_CONFIG=FS_DAX")

        assert "MEMKIND_MEM_TIERING_LOG_ERROR: Couldn't load MEMKIND_MEM_TIERING_CONFIG env var" in output, \
            "Wrong message"

    @pytest.mark.parametrize("pmem_size", ["as", "10K1", "M", "M2", "10KM"])
    def test_FSDAX_wrong_size(self, pmem_size):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=FS_DAX:/tmp/:" + pmem_size + ":1")

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Failed to parse pmem size: " + \
            pmem_size, "Wrong message"

    def test_FSDAX_negative_ratio(self):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=FS_DAX:/tmp/:10G:-1")

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Unsupported ratio: -1", "Wrong message"

    def test_FSDAX_wrong_ratio(self):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=FS_DAX:/tmp/:10G:a")

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Unsupported ratio: a", "Wrong message"

    def test_class_not_defined(self):
        output = self.get_cmd_output("MEMKIND_MEM_TIERING_CONFIG=2")

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Unsupported kind: 2", "Wrong message"

    def test_bad_ratio(self):
        output = self.get_cmd_output("MEMKIND_MEM_TIERING_CONFIG=DRAM:A")

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Unsupported ratio: A", "Wrong message"

    def test_no_ratio(self):
        output = self.get_cmd_output("MEMKIND_MEM_TIERING_CONFIG=DRAM")

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Ratio not provided", "Wrong message"

    def test_bad_class(self):
        output = self.get_cmd_output("MEMKIND_MEM_TIERING_CONFIG=a1b2:10")

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Unsupported kind: a1b2", "Wrong message"

    def test_negative_ratio(self):
        output = self.get_cmd_output("MEMKIND_MEM_TIERING_CONFIG=DRAM:-1")

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Unsupported ratio: -1", "Wrong message"

    @pytest.mark.parametrize("config_str", ["", ",", ",,,"])
    def test_negative_config_str_invalid(self, config_str):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=" + config_str)

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: No valid query found in: " + \
            config_str, "Wrong message"

    @pytest.mark.parametrize("query_str", [":", ":::"])
    def test_negative_query_str_invalid(self, query_str):
        output = self.get_cmd_output(
            "MEMKIND_MEM_TIERING_CONFIG=" + query_str)

        assert output.splitlines()[0] == \
            "MEMKIND_MEM_TIERING_LOG_ERROR: Kind name string not found in: " + \
            query_str, "Wrong message"
