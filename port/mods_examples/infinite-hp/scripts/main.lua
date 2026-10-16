-- Infinite HP: on every frame, the player's HP back to its maximum.
mod.on("frame", function()
  local hp, max = player.hp, player.max_hp
  if hp ~= nil and hp < max then
    player.set_hp(max)
  end
end)
