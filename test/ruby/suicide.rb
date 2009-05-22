STDERR.reopen(STDOUT)
at_exit{Process.kill(:INT, $$); sleep 0}
# brent@mbari.org says
#  sleep 0 avoids race between process termination and signal reception
