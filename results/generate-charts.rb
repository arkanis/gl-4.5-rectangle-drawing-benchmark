#!/usr/bin/ruby
require "pp"
require "erb"
require "csv"

# Inspired losely by https://stackoverflow.com/a/26858660
class AccessorHash
	def initialize(hash)
		@hash = hash
	end
	
	def method_missing(method)
		if @hash.key? method
			@hash.fetch method
		else
			super
		end
	end
end


def svg_chart(filename, bars:, values:, title: nil, style: {}, cutoff: nil)
	style = AccessorHash.new({
		margin: 10,
		title_text_size: 15,
		value_group_title_size: 11,
		bar_names_width: 150,
		bar_margin: 6, bar_group_text_size: 15, bar_height: 10, bar_text_size: 11,
		bar_colors: [
			"hsla(120, 50%, 50%, 0.5)",
			"hsla(0,   50%, 50%, 0.5)",
			"hsla(200, 50%, 50%, 0.5)",
			"hsla(280, 50%, 50%, 0.5)",
		],
		grid_lines_every: nil,
		grid_lines_value_format: "%.0fms",
		value_headroom: 50,
		value_text_margin: 5,
		value_text_size: 11,
		value_format: "%.2fms",
		value_limit: nil,
		value_scale: 1
	}.merge(style))
	
	if bars.kind_of? Hash
		bar_groups = bars
		background_per_bar_group = true
		background_per_bar_name = false
	else
		bar_groups = { "" => bars }
		background_per_bar_group = false
		background_per_bar_name = false #true
	end
	
	if values.kind_of? Hash
		value_groups = values
	else
		value_groups = { "" => values }
	end
	
	value_group_widths = value_groups.map do |title, values|
		values = values.filter{|v| v < cutoff} if cutoff
		width = (style.value_limit ? [values.max, style.value_limit].min : values.max) + style.value_headroom
		width *= style.value_scale
		if style.grid_lines_every
			grid_lines_every_px = style.grid_lines_every * style.value_scale
			width = (width.to_f / grid_lines_every_px).ceil * grid_lines_every_px
		end
		width
	end
	
	title_y      = style.margin
	title_height = (style.title_text_size * title.split("\n").size * 1.25).to_i
	
	value_group_titles_height = value_groups.keys.reject{|title| title == ""}.map{|title| title.split("\n").size}.max * style.value_group_title_size
	value_group_titles_y      = title_y + title_height + (value_group_titles_height > 0 ? style.margin : 0)
	
	bar_names_x = style.margin
	bar_names_y = value_group_titles_y + value_group_titles_height + style.margin + style.value_text_size
	
	grid_x          = style.margin + style.bar_names_width + style.margin
	grid_y          = bar_names_y
	grid_width      = value_group_widths.sum
	grid_height     = style.bar_margin + bar_groups.values.flatten.size * (style.bar_height + style.bar_margin) + bar_groups.keys.reject{|k| k == ""}.size * (style.bar_group_text_size + style.bar_margin)
	
	#title_x = (grid_x + grid_width + style.margin) / 2  # also needs text-anchor="middle" on <text class="title"> below
	title_x = style.margin
	
	svg = ERB.new(<<~EOD, trim_mode: "%<>").result(binding)
		<svg xmlns="http://www.w3.org/2000/svg" width="<%= grid_x + grid_width + style.margin %>" height="<%= grid_y + grid_height + style.margin %>">
			<defs>
				<style type="text/css">
					rect.background { fill: white; }
					rect.bar-background.odd { fill: hsl(0, 0%, 95%); }
					rect.bar-background.even { fill: none; }
					rect.grid-edge { fill: black; }
					rect.grid-line { fill: #ccc; }
					text.grid-value { fill: #ccc; }
					
					text { font-family: sans-serif; color: #333; }
				</style>
			</defs>
			<rect class="background" width="100%" height="100%" />
			
		<%	if title %>
			<text class="title" y="<%= title_y %>" font-size="<%= style.title_text_size %>">
		<%		title.split("\\n").each_with_index do |line, index| %>
				<tspan x="<%= title_x %>" dy="<%= (index == 0) ? 1.0 : 1.25 %>em"><%= line %></tspan>
		<%		end %>
			</text>
		<%	end %>
		
		<%	value_groups.keys.each_with_index do |value_group_title, value_group_index| %>
			<text class="value-group-title" y="<%= value_group_titles_y %>" font-size="<%= style.value_group_title_size %>" text-anchor="start">
		<%		line_x = grid_x + value_group_widths[0...value_group_index].sum %>
		<%		value_group_title.split("\\n").each_with_index do |line, index| %>
				<tspan x="<%= line_x %>" dy="<%= (index == 0) ? 1.0 : 1.0 %>em"><%= line %></tspan>
		<%		end %>
			</text>
		<%	end %>
			
		<%	x, y = bar_names_x, bar_names_y + style.bar_margin %>
		<%	bar_groups.each_with_index do |(bar_group_title, bar_names), bar_group_index| %>
		<%		if background_per_bar_group %>
			<rect class="bar-background <%= bar_group_index % 2 == 0 ? "even" : "odd" %>" x="<%= x %>" y="<%= y - style.bar_margin / 2 %>" width="<%= style.bar_names_width + style.margin + grid_width %>" height="<%= (bar_group_title.length > 0 ? 1 : 0) * (style.bar_group_text_size + style.bar_margin) + bar_names.size * (style.bar_height + style.bar_margin) %>" />
		<%		end %>
		<%		if bar_group_title.length > 0 %>
		<%			y += style.bar_group_text_size %>
			<text class="bar-title" x="<%= x + style.bar_names_width %>" y="<%= y %>" font-size="<%= style.bar_group_text_size %>" text-anchor="end"><%= bar_group_title %></text>
		<%			y += style.bar_margin %>
		<%		end %>
		<%		bar_names.each_with_index do |name, name_index| %>
		<%			if background_per_bar_name %>
			<rect class="bar-background <%= name_index % 2 == 0 ? "even" : "odd" %>" x="<%= x %>" y="<%= y - style.bar_margin / 2 %>" width="<%= style.bar_names_width + style.margin + grid_width %>" height="<%= style.bar_height + style.bar_margin %>" />
		<%			end %>
			<text class="bar-name" x="<%= x + style.bar_names_width %>" y="<%= y + (style.bar_height - style.bar_text_size) / 2 + style.bar_text_size - 1 %>" font-size="<%= style.bar_text_size %>" text-anchor="end"><%= name %></text>
		<%			y += style.bar_height + style.bar_margin %>
		<%		end %>
		<%	end %>
			
		<%	value_groups.each_with_index do |(value_group_title, values), value_group_index| %>
		<%		group_grid_x, group_grid_y = grid_x + value_group_widths[0...value_group_index].sum, grid_y %>
			<g class="grid" transform="translate(<%= group_grid_x %>, <%= group_grid_y %>)">
				<rect class="grid-edge" x="-1" y="0" width="1" height="<%= grid_height %>" />
				<!-- <rect class="grid-edge" x="0"  y="0" width="<%= value_group_widths[value_group_index] + 1 %>" height="1" /> -->
		<%		if style.grid_lines_every %>
		<%			(0 ... value_group_widths[value_group_index]).step(style.grid_lines_every * style.value_scale) do |x| %>
				<rect class="grid-line"  x="<%= x %>" y="1" width="1" height="<%= grid_height - 1 %>" />
				<text class="grid-value" x="<%= x %>" y="-2" font-size="<%= style.value_text_size %>" text-anchor="middle"><%= format(style.grid_lines_value_format, x / style.value_scale) %></text>
		<%			end %>
		<%		end %>
			</g>
			
		<%		x, y, value_index = group_grid_x, group_grid_y + style.bar_margin, 0 %>
		<%		bar_groups.each do |bar_group_title, bar_names| %>
		<%			y += style.bar_group_text_size + style.bar_margin if bar_group_title.length > 0 %>
		<%			bar_names.each do |name| %>
		<%				value = values[value_index] %>
		<%				value_width = ((style.value_limit and value > style.value_limit) ? style.value_limit : value) * style.value_scale %>
			<rect class="value" x="<%= x %>" y="<%= y %>" width="<%= value_width %>" height="<%= style.bar_height %>" fill="<%= style.bar_colors[value_index % style.bar_colors.size] %>" />
			<text class="value" x="<%= x + value_width + style.value_text_margin %>" y="<%= y + (style.bar_height - style.value_text_size) / 2 + style.value_text_size - 1 %>" font-size="<%= style.value_text_size %>"><%= format(style.value_format, value) %></text>
		<%				y += style.bar_height + style.bar_margin %>
		<%				value_index += 1 %>
		<%			end %>
		<%		end %>
		<%	end %>
		</svg>
	EOD
	File.write filename, svg
end



interesting_scenarios  = %w(sublime mediaplayer)
interesting_approaches = %w(1rect_1draw complete_vbo one_ssbo ssbo_instr_list ssbo_inlined_instr_6 one_ssbo_ext_one_sdf inst_div)
interesting_fields     = %w(approach_wt approach_ct  frame_wt  buffer_wt upload_ge  draw_wt draw_ge  pres_ge)
bar_colors             = %w(#73d216     #4e9a06      #8ae234   #c17d11   #e9b96e    #729fcf #3465a4  #ad7fa8)
interesting_results    = [
	"results-lin-dyson-on-ac-power",
	"results-win-DESKTOP-2SU0LLB Intel i5 5675C iGPU",
	"results-win-DESKTOP-TI3IMGT Intel A770",
	
	"results-win-Athlon-Nvidia-Test Nvidia GTX 770",
	"results-win-Erebus-2 Nvidia GTX 1080",
	"results-win-Mica-PC 3700X 4060Ti",
	
	"results-win-DESKTOP-4E8CN91 AMD 5700G iGPU AMD Treiber",
	"results-win-DESKTOP-4E8CN91 AMD 5700G iGPU Windows Treiber",
	"results-lin-steven",
	"results-win-Steven",
	"results-lin-michael-X570-AORUS-PRO AMD RX 5500",
	"results-win-Agamemnon Radeon Pro VII",
	"results-win-DESKTOP-H4ASG8L AMD A10-7850K iGPU",
	"results-win-DESKTOP-KCEARDT AMD R9 390X",
	"results-win-DESKTOP-KCEARDT AMD R9 Nao",
	"results-win-Lupus RX 6900XT",
]
#interesting_results    = Dir.glob("#{__dir__}/results-*/").map{|path| File.basename(path)}
field_descriptions = {
	approach_wt: "Approach walltime (with overhead)",
	approach_ct: "Approach CPU time (with overhead)",
	frame_wt:    "Frame CPU walltime",
	buffer_wt:   "Buffer generation CPU walltime",
	upload_ge:   "Buffer upload GPU elapsed time",
	draw_wt:     "Draw calls CPU walltime",
	draw_gt:     "Draw calls GPU time",
	draw_ge:     "Draw calls GPU elapsed time",
	pres_ge:     "Present GPU elapsed time"
}
scenario_descriptions = {
	sublime:     "Text editor scenario, 6400 rectangles, average area 519px.\nValues are averages over 5 benchmark runs rendering 100 frames each (without vsync).",
	mediaplayer: "Media player scenario, 53 rectangles, average area 55037px.\nValues are averages over 5 benchmark runs rendering 100 frames each (without vsync)."
}

value_groups_by_scenario = {}
interesting_scenarios.each{|scenario| value_groups_by_scenario[scenario] = {}}

interesting_results.map{|name| "#{__dir__}/#{name}/"}.each do |dir|
	# Skip benchmark runs where the benchmark aborted right away (e.g. no support for OpenGL 4.5 was present)
	next unless File.exists? "#{dir}glinfo.txt"
	
	os = File.basename(dir).split("-", 3)[1]
	if os == "lin"
		# model name	: Intel(R) Core(TM) i7-8550U CPU @ 1.80GHz
		cpu_name = File.readlines("#{dir}cpuinfo.txt").find{|l| l.start_with? "model name	:"}.split(":", 2).last.strip
	elsif os == "win"
		# Name=AMD Ryzen Threadripper 3960X 24-Core Processor 
		# See https://ruby-doc.org/2.7.8/IO.html#method-c-new "IO Encoding" for details about the mode format: fmode ":" ext_enc ":" int_enc
		cpu_name = File.readlines("#{dir}cpuinfo.txt", mode: "rb:UTF-16:UTF-8").find{|l| l.start_with? "Name="}.split("=", 2).last.strip
	else
		abort "unknown os: #{os}"
	end
	gpu_name = File.readlines("#{dir}glinfo.txt").find{|l| l.start_with? "gl renderer"}.split(", ", 2).last
	
	# Clean up CPU names:
	# Intel(R) Core(TM) i7-8550U CPU @ 1.80GHz
	# Intel(R) Xeon(R) CPU E5-2697 v2 @ 2.70GHz
	# Intel(R) Xeon(R) CPU E5-2697 v2 @ 2.70GHz
	# AMD Ryzen 7 3700X 8-Core Processor
	# AMD Ryzen 7 5700X 8-Core Processor
	# AMD Ryzen Threadripper 3960X 24-Core Processor
	# AMD Athlon(tm) II X4 640 Processor
	# AMD Ryzen 7 5700G with Radeon Graphics
	# AMD Ryzen 7 5700G with Radeon Graphics
	# AMD A10-7850K Radeon R7, 12 Compute Cores 4C+8G
	# AMD Ryzen 7 5700X 8-Core Processor
	# AMD Ryzen 9 7900X 12-Core Processor
	# AMD Ryzen 9 5900X 12-Core Processor
	# AMD Ryzen 7 5700X 8-Core Processor
	#cpu_name = cpu_name.gsub(/\s*\(\w+\)/, "").gsub(/^(Intel|AMD|NVIDIA)\s+/i, "").gsub(/ (CPU )?@ \d+\.\d+GHz| (\d+-Core )?Processor/, "")
	
	# Clean up GPU names:
	# Mesa Intel(R) UHD Graphics 620 (KBL GT2)
	# AMD Radeon RX 5500 XT (navi14, LLVM 15.0.7, DRM 3.47, 5.19.0-45-generic)
	# AMD Radeon RX 480 Graphics (polaris10, LLVM 15.0.6, DRM 3.42, 5.15.0-67-generic)
	# AMD Radeon Pro VII
	# GeForce GTX 770/PCIe/SSE2
	# AMD Radeon(TM) Graphics
	# AMD Radeon(TM) Graphics
	# AMD Radeon(TM) R7 Graphics
	# AMD Radeon (TM) R9 390 Series
	# AMD Radeon (TM) R9 Fury Series
	# Intel(R) Arc(TM) A770 Graphics
	# NVIDIA GeForce GTX 1080/PCIe/SSE2
	# AMD Radeon RX 6900 XT
	# Radeon (TM) RX 480 Graphics
	#gpu_name = gpu_name.gsub(/\s*\(\w+\)/, "").gsub(/^(Intel|AMD|NVIDIA)\s+/i, "").gsub(/ \(.*\)/, "").gsub(/ Graphics| Series/, "")
	
	puts dir
	puts "    #{cpu_name}, #{gpu_name}"
	
	values = File.read("#{dir}bench-approaches.csv").split("\n").map do |line|
		line.split(",").map{|value| value.strip}
	end
	values_header = values.shift
	values_by_scenario_and_approach = values.group_by{|scenario, approach, *rest| "#{scenario}-#{approach}"}
	
	value_group_title = "#{File.basename(dir)}\n#{cpu_name}\n#{gpu_name}"
	field_indices = interesting_fields.map{|name| values_header.find_index(name)}
	interesting_scenarios.each do |scenario|
		scenario_values = interesting_approaches.map do |approach|
			approach_values = values_by_scenario_and_approach["#{scenario}-#{approach}"]
			if approach_values
				field_indices.map do |field_index|
					approach_values.map{|values| values[field_index].to_f}.sum / approach_values.size
				end
			else
				field_indices.map{ 0 }
			end
		end.flatten
		
		value_groups_by_scenario[scenario][value_group_title] = scenario_values
	end
end

bars = interesting_approaches.map do |approach|
	[approach, interesting_fields.map{|name| field_descriptions[name.to_sym]}]
end.to_h

value_groups_by_scenario.each do |scenario, value_groups|
	puts "#{scenario}.svg"
	svg_chart "#{scenario}.svg",
		title: scenario_descriptions[scenario.to_sym],
		bars: bars,
		values: value_groups.map{|name, values| [name, values.map{|v| v.to_f / 100}]}.to_h,
		style: {
			value_group_title_size: 8, bar_names_width: 200, bar_colors: bar_colors,
			value_scale: 100, value_headroom: 1, grid_lines_every: 1, value_format: "%.3fms"
		}
end

value_groups_by_scenario.each do |scenario, value_groups|
	puts "#{scenario}-normalized.svg"
	
	ref_approach_index = interesting_approaches.find_index("one_ssbo_ext_one_sdf")
	normalized_value_groups = value_groups.each_with_index.map do |(group_title, values)|
		normalized_values = values.each_with_index.map do |value, index|
			interesting_field_index = index % interesting_fields.length
			interesting_approach_index = index / interesting_fields.length
			value / values[ref_approach_index * interesting_fields.length + interesting_field_index]
		end
		[group_title, normalized_values]
	end.to_h
	
	svg_chart "#{scenario}-normalized.svg",
		title: scenario_descriptions[scenario.to_sym],
		bars: bars,
		values: normalized_value_groups,
		style: { value_group_title_size: 8, bar_names_width: 200, value_limit: 5, value_scale: 100, grid_lines_every: 0.5, grid_lines_value_format: "%.1f", value_headroom: 0.5, value_format: "%.2f", bar_colors: bar_colors }
end