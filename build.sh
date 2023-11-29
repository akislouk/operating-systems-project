#!/bin/sh
make clean
make
echo "Build complete"

# Parse command-line options
while getopts ":s" opt; do
  case $opt in
    s)
      skip=true
      ;;
    \?)
      echo "Invalid option: -$OPTARG" >&2
      exit 1
      ;;
    :)
      echo "Option -$OPTARG requires an argument." >&2
      exit 1
      ;;
  esac
done

# Skip tests if the -s option is given
if [ "$skip" = true ]; then
  echo "Skipping tests"
  exit 0
fi

# Run tests
./validate_api -c 1,2,4 basic_tests
gnome-terminal -- bash -c "./terminal 0"
gnome-terminal -- bash -c "./terminal 1"
./validate_api -c 1,2,4 --term=0,2 thread_tests
./tinyos_shell 4 2
