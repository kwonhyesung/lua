return function(self, event)
  self.IsOpen = not self.IsOpen
  self:ApplyState()
end
