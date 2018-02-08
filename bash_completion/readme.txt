
Starting with release 3.2, picocom includes support for custom
bash-shell completion. With this you can press the [TAB] key and have
the bash shell complete command-line option names and values and
propose valid selections for both. This makes the experience of using
picocom much more pleasant.

Custom bash-shell completion works only with recent versions of the
bash shell (>= 4.3).

To manually enable custom completion support you need to source the
file (custom completion script):

    <picocom source dir>/bash_completion/picocom

Assuming you are inside the picocom source directory, you can do it
like this:

    . ./bash_completion/picocom

This will enable custom completion support for the current shell
session only. Give in a ride and see if you like it.

To enable support automatically for all bash-shell sessions, you have
the following options:

1. If you are running a relatively modern Debian or Ubuntu or other
   Debian-based distribution, you can simply copy the custom
   completion script to the directory:

       /etc/bash_completion.d/

   Obviously, you need to be root to do this. Assuming you are inside
   the picocom source directory, something like this will do it:

      sudo cp ./bash_completion/picocom /etc/bash_completion.d/

   This will enable custom completion support for picocom, globaly
   (for all bash-shell users).

   For other distributions and operating systems you have to check
   their documentation to see if they provide a similar mechanism for
   automatically sourcing custom completion scripts.

2. If you want to automatically enable support *only for the current
   user*, you must arange for your user's `.bashrc` to source the
   custom completion script. There are, obviously, many ways to do
   this, so the following *is only a suggestion*:

   Create a directory to keep the custom completion scripts

       mkdir ~/.bash_completion.d

   Copy the picocom completion script to the directory you
   created. Assuming you are inside the picocom source directory:

       cp ./bash_completion/picocom ~/.bash_completion.d

   Add the following to the end of your `.bashrc`

       # Source custom bash completions
       if [ -d "$HOME"/.bash_completion.d ]; then
           for c in "$HOME"/.bash_completion.d/*; do
               [ -r "$c" ] && . "$c"
           done
       fi

   From now on every new shell session you start will load (source)
   all the custom completion scripts you have put in
   `~/.bash_completion.d`
