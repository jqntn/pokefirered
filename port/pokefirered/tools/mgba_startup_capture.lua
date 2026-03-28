-- mGBA startup capture helper for ROM-vs-port comparisons.
--
-- Usage from the mGBA scripting console after loading the ROM:
--   local capture = dofile([[C:\path\to\mgba_startup_capture.lua]])
--   capture.run_manifest(
--     [[C:\path\to\startup_frame_manifest.txt]],
--     [[C:\temp\fr-rom-startup]])
--
-- Optional auto-START input:
--   capture.run_manifest(
--     [[C:\path\to\startup_frame_manifest.txt]],
--     [[C:\temp\fr-rom-startup]],
--     { auto_press_start_frames = { 1860, 1880, 1900 } })
--
-- Optional autorun:
--   PFR_STARTUP_CAPTURE_ARGS = {
--     manifest = [[C:\path\to\startup_frame_manifest.txt]],
--     output_dir = [[C:\temp\fr-rom-startup]],
--   }
--   dofile([[C:\path\to\mgba_startup_capture.lua]])

local PfrStartupCapture = {}

local PATH_SEPARATOR = package.config:sub(1, 1)
local START_MASK = util.makeBitmask({ C.GBA_KEY.START })

local function trim(text)
  return (text:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function sanitize_name(name)
  local sanitized = name:gsub("[^%w_-]", "_")

  if sanitized == "" then
    return "frame"
  end

  return sanitized
end

local function join_path(lhs, rhs)
  if lhs:sub(-1) == "/" or lhs:sub(-1) == "\\" then
    return lhs .. rhs
  end

  return lhs .. PATH_SEPARATOR .. rhs
end

local function ensure_directory(path)
  if path:find("\"", 1, true) then
    error("output paths containing double quotes are not supported")
  end

  if PATH_SEPARATOR == "\\" then
    os.execute('mkdir "' .. path .. '" >NUL 2>NUL')
  else
    os.execute('mkdir -p "' .. path .. '" >/dev/null 2>&1')
  end
end

local function parse_manifest(path)
  local captures = {}
  local line_number = 0

  for raw_line in io.lines(path) do
    local line = trim(raw_line)

    line_number = line_number + 1
    if line ~= "" and line:sub(1, 1) ~= "#" then
      local name, frame = line:match("^([^|]+)|([^|]+)")

      if not name or not frame then
        error(string.format("invalid manifest line %d in %s", line_number, path))
      end

      frame = tonumber(trim(frame), 10)
      if not frame then
        error(string.format("invalid frame on line %d in %s", line_number, path))
      end

      captures[#captures + 1] = {
        name = trim(name),
        frame = frame,
      }
    end
  end

  if #captures == 0 then
    error("manifest did not define any frames: " .. path)
  end

  return captures
end

local function normalize_captures(args)
  if args.captures then
    return args.captures
  end

  if args.manifest then
    return parse_manifest(args.manifest)
  end

  error("expected captures or manifest")
end

local function group_captures_by_frame(captures, output_dir)
  local grouped = {}
  local max_frame = 0
  local i

  for i = 1, #captures do
    local capture = captures[i]
    local bucket = grouped[capture.frame]
    local filename

    if not bucket then
      bucket = {}
      grouped[capture.frame] = bucket
    end

    if capture.path then
      filename = capture.path
    else
      filename = string.format("%06d_%s.png",
                               capture.frame,
                               sanitize_name(capture.name))
      capture.path = join_path(output_dir, filename)
    end

    bucket[#bucket + 1] = {
      name = capture.name,
      frame = capture.frame,
      path = capture.path,
      manifest_path = filename,
    }

    if capture.frame > max_frame then
      max_frame = capture.frame
    end
  end

  return grouped, max_frame
end

local function make_frame_set(frames)
  local frame_set = {}
  local i

  if not frames then
    return frame_set
  end

  for i = 1, #frames do
    frame_set[frames[i]] = true
  end

  return frame_set
end

local function log(message)
  console:log("[pfr-startup-capture] " .. message)
end

function PfrStartupCapture.run(args)
  local captures = normalize_captures(args)
  local output_dir = assert(args.output_dir, "expected output_dir")
  local manifest_out =
    args.manifest_out or join_path(output_dir, "capture_manifest.txt")
  local auto_press_start_frames = make_frame_set(args.auto_press_start_frames)
  local captures_by_frame
  local max_frame
  local manifest_file
  local frame_index

  if not emu then
    error("load a ROM before running startup capture")
  end

  ensure_directory(output_dir)
  captures_by_frame, max_frame = group_captures_by_frame(captures, output_dir)
  manifest_file = assert(io.open(manifest_out, "w"))

  if args.reset ~= false then
    emu:reset()
  end

  emu:setKeys(0)

  for frame_index = 0, max_frame do
    if auto_press_start_frames[frame_index] then
      emu:setKeys(START_MASK)
    else
      emu:setKeys(0)
    end

    emu:runFrame()

    if captures_by_frame[frame_index] then
      local bucket = captures_by_frame[frame_index]
      local i

      for i = 1, #bucket do
        local capture = bucket[i]

        emu:screenshot(capture.path)
        manifest_file:write(string.format("%s|%d|%s\n",
                                          capture.name,
                                          capture.frame,
                                          capture.manifest_path))
        log(string.format("captured %s at frame %d",
                          capture.name,
                          capture.frame))
      end
    end
  end

  emu:setKeys(0)
  manifest_file:close()
  log("wrote manifest to " .. manifest_out)
end

function PfrStartupCapture.run_manifest(manifest_path, output_dir, options)
  options = options or {}
  options.manifest = manifest_path
  options.output_dir = output_dir
  return PfrStartupCapture.run(options)
end

_G.PfrStartupCapture = PfrStartupCapture

if rawget(_G, "PFR_STARTUP_CAPTURE_ARGS") then
  PfrStartupCapture.run(PFR_STARTUP_CAPTURE_ARGS)
end

return PfrStartupCapture
