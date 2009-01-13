require 'distro/tasks'

verbose true

desc "Create a patch for upstream Ruby"
task :make_patch do
	require 'yaml'
	branch_data = YAML::load_file('branch.yml')
	upstream = ENV['upstream'] || 'master'
	files = `git diff #{upstream} --stat | grep '|' | awk '{ print $1 }'`.split("\n")
	files -= branch_data['exclude_from_patch'].map { |x| Dir[x] }.flatten
	sh "git", "diff", "#{upstream}", *files
end

desc "Change shebang lines for Ruby scripts to '#!/usr/bin/env ruby'"
task :fix_shebang do
	if ENV['dir'].nil?
		STDERR.write("Usage: rake fix_shebang dir=<SOME DIRECTORY>\n")
		exit 1
	end
	Dir.chdir(ENV['dir']) do
		sh "sed -i 's|^#!.*$|#!/usr/bin/env ruby|' *"
	end
end
