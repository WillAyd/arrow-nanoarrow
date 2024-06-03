# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

import argparse
import pathlib
import re


# Generate the nanoarrow_c.pxd file used by the Cython extensions
class NanoarrowPxdGenerator:
    def __init__(self):
        self._define_regexes()

    def generate_nanoarrow_pxd(self, content: str, build_dir: pathlib.Path) -> None:
        # Strip comments
        content = self.re_comment.sub("", content)

        # Replace NANOARROW_MAX_FIXED_BUFFERS with its value
        content = self.re_max_buffers.sub("3", content)

        # Find typedefs, types, and function definitions
        typedefs = self._find_typedefs(content)
        types = self._find_types(content)
        func_defs = self._find_func_defs(content)

        # Make corresponding cython definitions
        typedefs_cython = [self._typdef_to_cython(t, "    ") for t in typedefs]
        types_cython = [self._type_to_cython(t, "    ") for t in types]
        func_defs_cython = [self._func_def_to_cython(d, "    ") for d in func_defs]

        # Unindent the header
        header = self.re_newline_plus_indent.sub("\n", self._pxd_header())

        # Write nanoarrow_c.pxd
        file_out = build_dir / "nanoarrow_c.pxd"
        with open(file_out, "wb") as output:
            output.write(header.encode("UTF-8"))

            output.write('\ncdef extern from "nanoarrow.h" nogil:\n'.encode("UTF-8"))

            # A few things we add in manually
            output.write(b"\n")
            output.write(b"    cdef int NANOARROW_OK\n")
            output.write(b"    cdef int NANOARROW_MAX_FIXED_BUFFERS\n")
            output.write(b"    cdef int ARROW_FLAG_DICTIONARY_ORDERED\n")
            output.write(b"    cdef int ARROW_FLAG_NULLABLE\n")
            output.write(b"    cdef int ARROW_FLAG_MAP_KEYS_SORTED\n")
            output.write(b"\n")

            for type in types_cython:
                output.write(type.encode("UTF-8"))
                output.write(b"\n\n")

            for typedef in typedefs_cython:
                output.write(typedef.encode("UTF-8"))
                output.write(b"\n")
            output.write(b"\n")

            for func_def in func_defs_cython:
                output.write(func_def.encode("UTF-8"))
                output.write(b"\n")

    def _define_regexes(self):
        self.re_comment = re.compile(r"\s*//[^\n]*")
        self.re_max_buffers = re.compile(r"NANOARROW_MAX_FIXED_BUFFERS")
        self.re_typedef = re.compile(r"typedef(?P<typedef>[^;]+)")
        self.re_type = re.compile(
            r"(?P<type>struct|union|enum) (?P<name>Arrow[^ ]+) {(?P<body>[^}]*)}"
        )
        self.re_func_def = re.compile(
            r"\n(static inline )?(?P<const>const )?(struct |enum )?"
            r"(?P<return_type>[A-Za-z0-9_*]+) "
            r"(?P<name>Arrow[A-Za-z0-9]+)\((?P<args>[^\)]*)\);"
        )
        self.re_tagged_type = re.compile(
            r"(?P<type>struct|union|enum) (?P<name>Arrow[A-Za-z]+)"
        )
        self.re_struct_delim = re.compile(r";\s*")
        self.re_enum_delim = re.compile(r",\s*")
        self.re_whitespace = re.compile(r"\s+")
        self.re_newline_plus_indent = re.compile(r"\n +")

    def _strip_comments(self, content):
        return self.re_comment.sub("", content)

    def _find_typedefs(self, content):
        return [m.groupdict() for m in self.re_typedef.finditer(content)]

    def _find_types(self, content):
        return [m.groupdict() for m in self.re_type.finditer(content)]

    def _find_func_defs(self, content):
        return [m.groupdict() for m in self.re_func_def.finditer(content)]

    def _typdef_to_cython(self, t, indent=""):
        typedef = t["typedef"]
        typedef = self.re_tagged_type.sub(r"\2", typedef)
        return f"{indent}ctypedef {typedef}"

    def _type_to_cython(self, t, indent=""):
        type = t["type"]
        name = t["name"]
        body = self.re_tagged_type.sub(r"\2", t["body"].strip())
        if type == "enum":
            items = [item for item in self.re_enum_delim.split(body) if item]
        else:
            items = [item for item in self.re_struct_delim.split(body) if item]

        cython_body = f"\n{indent}    ".join([""] + items)
        return f"{indent}{type} {name}:{cython_body}"

    def _func_def_to_cython(self, d, indent=""):
        return_type = d["return_type"].strip()
        if d["const"]:
            return_type = "const " + return_type
        name = d["name"]
        args = re.sub(r"\s+", " ", d["args"].strip())
        args = self.re_tagged_type.sub(r"\2", args)

        # Cython doesn't do (void)
        if args == "void":
            args = ""

        return f"{indent}{return_type} {name}({args})"

    def _pxd_header(self):
        return """
        # Licensed to the Apache Software Foundation (ASF) under one
        # or more contributor license agreements.  See the NOTICE file
        # distributed with this work for additional information
        # regarding copyright ownership.  The ASF licenses this file
        # to you under the Apache License, Version 2.0 (the
        # "License"); you may not use this file except in compliance
        # with the License.  You may obtain a copy of the License at
        #
        #   http://www.apache.org/licenses/LICENSE-2.0
        #
        # Unless required by applicable law or agreed to in writing,
        # software distributed under the License is distributed on an
        # "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
        # KIND, either express or implied.  See the License for the
        # specific language governing permissions and limitations
        # under the License.

        # cython: language_level = 3

        from libc.stdint cimport int8_t, uint8_t, int16_t, uint16_t
        from libc.stdint cimport int32_t, uint32_t, int64_t, uint64_t
        """


def generate_nanoarrow_c() -> str:
    this_dir = pathlib.Path(__file__).parent.resolve()
    nanoarrow_dir = this_dir / "subprojects" / "nanoarrow" / "src" / "nanoarrow"

    # This should match the NANOARROW_BUNDLE code in CMakeLists.txt
    # With the only thing missing being the nanoarrow namespace. However, we
    # assume the Python installation is sandboxed so should not be required (?)
    header_data: list[str] = []

    files = [
        # TODO: - do we need the config file for Cython?
        # 'nanoarrow_config.h',
        "nanoarrow_types.h",
        "nanoarrow.h",
        "buffer_inline.h",
        "array_inline.h",
    ]

    for file in files:
        with open(nanoarrow_dir / file) as f:
            header_data.append(f.read())

    contents = "\n".join(header_data)
    # Remove includes that aren't needed when the headers are concatenated
    contents = re.sub(r"#include \".*", "", contents)

    return contents


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=str)
    args = parser.parse_args()
    build_dir = pathlib.Path(args.build_dir).resolve()

    contents = generate_nanoarrow_c()
    NanoarrowPxdGenerator().generate_nanoarrow_pxd(contents, build_dir)
