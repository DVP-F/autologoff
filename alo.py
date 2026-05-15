from subprocess import run, check_output, CalledProcessError, DEVNULL
from time import sleep

class enum_parsed_types:
	username = 0
	sessionid = 1
	idletime = 2
	state = 3

def get_sessions() -> tuple[dict[enum_parsed_types,str]] :
	try: # `query user` returns exitcode 0x1, so this is correct parsing.
		check_output("query user 2>nul", shell=True, errors=None, stderr=DEVNULL)
	except CalledProcessError as e:
		if e.returncode == 1: # split up expose the types
			stdout:bytes = e.output; out:tuple[dict[enum_parsed_types,str]] = {}; content = stdout.decode('utf-8', errors='ignore').splitlines()[1:]
			for i in range(len(content)): 
				l_c = content[i].split()
				out[i] = {enum_parsed_types.username: l_c[0][1:], enum_parsed_types.sessionid: l_c[2], enum_parsed_types.idletime: l_c[4], enum_parsed_types.state: l_c[3]}
			return out
		raise ChildProcessError("`query user 2>nul` returned non-1 exit code")
	raise ChildProcessError("`query user 2>nul` exited with code '0'")

def main():
	while True:
		try:
			print(get_sessions())
			sleep(5)
		except KeyboardInterrupt: return # quiet ^C

if __name__ == "__main__": main()
