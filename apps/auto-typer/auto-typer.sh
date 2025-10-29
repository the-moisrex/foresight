#!/bin/bash

# This file will be included in every auto typer script.

# Function to list only user-defined variables (excluding functions, environment variables, and internal shell variables)
list_user_variables() {
  # List all variables and filter out function names and readonly/special ones using declare -p
  # Exclude variables declared as functions and special variables
  compgen -v | while read -r var; do
    # Check if the variable is not a function and is set by user
    if ! declare -F "$var" >/dev/null 2>&1 && [[ "$var" != BASH_* && "$var" != EUID && "$var" != PPID && "$var" != UID && "$var" != SHELLOPTS && "$var" != PATH && "$var" != HOME ]]; then
      echo "$var"
    fi
  done
}

# Function to list only user-defined functions
list_user_functions() {
  # Use declare -F to list function names only
  declare -F | awk '{print $3}'
}
