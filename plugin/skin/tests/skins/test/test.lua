-- Test skin script: records what the panel API reports, for skin_test.
events = {}
function onLoad()
  panel.onButton(8, function(down) table.insert(events, "button8 " .. tostring(down)) end)
  panel.onEncoder(function(d) table.insert(events, "encoder " .. d) end)
  panel.onMaster(function(raw) table.insert(events, "master " .. raw) end)
  panel.onLed(21, function(level) table.insert(events, "led21 " .. level) end)
  panel.onDisplay(function(a, b, c, d) table.insert(events, string.format("display %d %d %d %d", a, b, c, d)) end)
end
function lastEvent() return events[#events] end
