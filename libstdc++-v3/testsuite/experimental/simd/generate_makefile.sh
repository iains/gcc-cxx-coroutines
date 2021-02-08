#!/bin/sh

srcdir="$(cd "${0%/*}" && pwd)"
driver="$srcdir/driver.sh"
srcdir="$srcdir/tests"
sim=
rm_logs=true
dst=.
testflags=

usage() {
  cat <<EOF
Usage: $0 [Options] <g++ invocation>

Options:
  -h, --help          Print this message and exit.
  --srcdir <path>     The source directory of the tests (default: $srcdir).
  --sim <executable>  Path to an executable that is prepended to the test
                      execution binary (default: none).
  --keep-intermediate-logs
                      Keep intermediate logs.
  --testflags <flags> Force initial TESTFLAGS contents.
  -d <path>, --destination <path>
                      Destination for the generated Makefile. If the directory
                      does not exist it is created (default: $dst).
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
  -h|--help)
    usage
    exit
    ;;
  --testflags)
    testflags="$2"
    shift
    ;;
  --testflags=*)
    testflags="${1#--testflags=}"
    ;;
  -d|--destination)
    dst="$2"
    shift
    ;;
  --destination=*)
    dst="${1#--destination=}"
    ;;
  --keep-intermediate-logs)
    rm_logs=false
    ;;
  --srcdir)
    srcdir="$2"
    shift
    ;;
  --srcdir=*)
    srcdir="${1#--srcdir=}"
    ;;
  --sim)
    sim="$2"
    shift
    ;;
  --sim=*)
    sim="${1#--sim=}"
    ;;
  --)
    shift
    break
    ;;
  *)
    break
    ;;
  esac
  shift
done

mkdir -p "$dst"
dst="$dst/Makefile"
if [ -f "$dst" ]; then
  echo "Error: $dst already exists. Aborting." 1>&2
  exit 1
fi

CXX="$1"
shift

echo "TESTFLAGS ?=" > "$dst"
[ -n "$testflags" ] && echo "TESTFLAGS := $testflags \$(TESTFLAGS)" >> "$dst"
echo CXXFLAGS = "$@" "\$(TESTFLAGS)" >> "$dst"
[ -n "$sim" ] && echo "export GCC_TEST_SIMULATOR = $sim" >> "$dst"
cat >> "$dst" <<EOF
srcdir = ${srcdir}
CXX = ${CXX}
DRIVER = ${driver}
DRIVEROPTS ?=

all: simd_testsuite.sum

simd_testsuite.sum: simd_testsuite.log
	@printf "\n\t\t=== simd_testsuite \$(TESTFLAGS) Summary ===\n\n"\\
	"# of expected passes:\t\t\$(shell grep -c '^PASS:' \$@)\n"\\
	"# of unexpected failures:\t\$(shell grep -c '^FAIL:' \$@)\n"\\
	"# of unsupported tests:\t\t\$(shell grep -c '^UNSUPPORTED:' \$@)\n"\\
	  | tee -a \$@

EOF

all_types() {
  src="$1"
  cat <<EOF
long double
ldouble
double
double
float
float
EOF
  ([ -n "$src" ] && grep -q "test only floattypes" "$src") || \
  cat <<EOF
long long
llong
unsigned long long
ullong
unsigned long
ulong
long
long
int
int
unsigned int
uint
short
short
unsigned short
ushort
char
char
signed char
schar
unsigned char
uchar
char32_t
char32_t
char16_t
char16_t
wchar_t
wchar_t
EOF
}

all_tests() {
  if [ -f testsuite_files_simd ]; then
    sed 's,^experimental/simd/tests/,,' testsuite_files_simd | while read file; do
      echo "$srcdir/$file"
      echo "${file%.cc}"
    done
  else
    for file in ${srcdir}/*.cc; do
      echo "$file"
      name="${file%.cc}"
      echo "${name##*/}"
    done
  fi
}

{
  rmline=""
  if $rm_logs; then
    rmline="
	@rm \$^ \$(^:log=sum)"
  fi
  echo -n "simd_testsuite.log:"
  all_tests | while read file && read name; do
    echo -n " $name.log"
  done
  cat <<EOF

	@cat $^ > \$@
	@cat \$(^:log=sum) > \$(@:log=sum)${rmline}

EOF
  all_tests | while read file && read name; do
    echo -n "$name.log:"
    all_types "$file" | while read t && read type; do
      echo -n " $name-$type.log"
    done
    cat <<EOF

	@cat $^ > \$@
	@cat \$(^:log=sum) > \$(@:log=sum)${rmline}

EOF
  done
  all_types | while read t && read type; do
    cat <<EOF
%-$type.log: %-$type-0.log %-$type-1.log %-$type-2.log %-$type-3.log \
%-$type-4.log %-$type-5.log %-$type-6.log %-$type-7.log \
%-$type-8.log %-$type-9.log
	@cat $^ > \$@
	@cat \$(^:log=sum) > \$(@:log=sum)${rmline}

EOF
    for i in $(seq 0 9); do
      cat <<EOF
%-$type-$i.log: \$(srcdir)/%.cc
	@\$(DRIVER) \$(DRIVEROPTS) -t "$t" -a $i -n \$* \$(CXX) \$(CXXFLAGS)

EOF
    done
  done
  echo 'run-%: export GCC_TEST_RUN_EXPENSIVE=yes'
  all_tests | while read file && read name; do
    echo "run-$name: $name.log"
    all_types "$file" | while read t && read type; do
      echo "run-$name-$type: $name-$type.log"
      for i in $(seq 0 9); do
        echo "run-$name-$type-$i: $name-$type-$i.log"
      done
    done
    echo
  done
  cat <<EOF
help:
	@printf "use DRIVEROPTS=<options> to pass the following options:\n"\\
	"-q, --quiet         Only print failures.\n"\\
	"-v, --verbose       Print compiler and test output on failure.\n"\\
	"-k, --keep-failed   Keep executables of failed tests.\n"\\
	"--sim <executable>  Path to an executable that is prepended to the test\n"\\
	"                    execution binary (default: the value of\n"\\
	"                    GCC_TEST_SIMULATOR).\n"\\
	"--timeout-factor <x>\n"\\
	"                    Multiply the default timeout with x.\n"\\
	"--run-expensive     Compile and run tests marked as expensive (default:\n"\\
	"                    true if GCC_TEST_RUN_EXPENSIVE is set, false otherwise).\n"\\
	"--only <pattern>    Compile and run only tests matching the given pattern.\n\n"
	@echo "use TESTFLAGS=<flags> to pass additional compiler flags"
	@echo
	@echo "The following are some of the valid targets for this Makefile:"
	@echo "... all"
	@echo "... clean"
	@echo "... help"
EOF
  all_tests | while read file && read name; do
    printf "\t@echo '... run-${name}'\n"
    all_types | while read t && read type; do
      printf "\t@echo '... run-${name}-${type}'\n"
      for i in $(seq 0 9); do
        printf "\t@echo '... run-${name}-${type}-$i'\n"
      done
    done
  done
  cat <<EOF

clean:
	rm -f -- *.sum *.log *.exe

.PHONY: clean help

.PRECIOUS: %.log %.sum
EOF
} >> "$dst"

