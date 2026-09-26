-- 원본 동작은 그대로 두고, HP/MP/최대치가 갱신될 때마다 값을 ___MOD._G 브리지에도 저장해둔다.
-- UI 위젯(UIMeramHUDNumberByPreset)은 순수 디스플레이용이라 현재값을 저장 안 해서 나중에 못 읽는다 —
-- 그래서 여기(서버→클라 값이 실제로 지나가는 지점)에서 가로채 캐싱한다.
-- (MeramTargettingService.OnUpdate의 자힐 로직이 ___MOD._G.__LuahookFollow.myHp 등을 읽는다)
return function(self, propname, value)
  if propname == ___MOD._MeramHudService.ESyncPropHp
      or propname == ___MOD._MeramHudService.ESyncPropMaxHp
      or propname == ___MOD._MeramHudService.ESyncPropMp
      or propname == ___MOD._MeramHudService.ESyncPropMaxMp then
    local bridge = ___MOD._G.__LuahookFollow
    if not bridge then
      bridge = {}
      ___MOD._G.__LuahookFollow = bridge
    end
    if propname == ___MOD._MeramHudService.ESyncPropHp then
      bridge.myHp = value
    elseif propname == ___MOD._MeramHudService.ESyncPropMaxHp then
      bridge.myMaxHp = value
    elseif propname == ___MOD._MeramHudService.ESyncPropMp then
      bridge.myMp = value
    else
      bridge.myMaxMp = value
    end
  end

  if propname == ___MOD._MeramHudService.ESyncPropName then
    self.Name.Text = value
  elseif propname == ___MOD._MeramHudService.ESyncPropClass then
    self.Class.ImageRUID = ___MOD._MeramHudService:GetSpriteRUID("stat_class_" .. ___MOD.tostring(value))
  elseif propname == ___MOD._MeramHudService.ESyncPropTotem then
    self.Totem.ImageRUID = ___MOD._MeramHudService:GetSpriteRUID("stat_totem_" .. ___MOD.tostring(value))
  elseif propname == ___MOD._MeramHudService.ESyncPropNation then
    self.Nation.ImageRUID = ___MOD._MeramHudService:GetSpriteRUID("stat_nation_" .. ___MOD.tostring(value))
  elseif propname == ___MOD._MeramHudService.ESyncPropLevel then
    self.Level:SetNumber(value)
  elseif propname == ___MOD._MeramHudService.ESyncPropStr then
    self.Str:SetNumber(value)
  elseif propname == ___MOD._MeramHudService.ESyncPropDex then
    self.Dex:SetNumber(value)
  elseif propname == ___MOD._MeramHudService.ESyncPropInt then
    self.Int:SetNumber(value)
  elseif propname == ___MOD._MeramHudService.ESyncPropMaxHp then
    self.Hp:SetMax(value)
  elseif propname == ___MOD._MeramHudService.ESyncPropHp then
    self.Hp:Set(value)
  elseif propname == ___MOD._MeramHudService.ESyncPropMaxMp then
    self.Mp:SetMax(value)
  elseif propname == ___MOD._MeramHudService.ESyncPropMp then
    self.Mp:Set(value)
  elseif propname == ___MOD._MeramHudService.ESyncPropExp then
    self.Exp:SetNumber(value)
  elseif propname == ___MOD._MeramHudService.ESyncPropGold then
    self.Funds:SetNumber(value)
  end
end
