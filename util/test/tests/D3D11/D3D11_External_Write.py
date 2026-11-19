import renderdoc as rd
import rdtest


class D3D11_External_Write(rdtest.TestCase):
    demos_test_name = 'D3D11_External_Write'

    def check_capture(self):
        first = self.find_action('Restore captured First output')
        second = self.find_action('Restore captured Second output')
        self.check(first is not None and second is not None)
        self.check(first.eventId < second.eventId)
        self.check(first.flags & rd.ActionFlags.Copy)
        self.check(not first.flags & (rd.ActionFlags.Dispatch | rd.ActionFlags.Drawcall))
        self.controller.SetFrameEvent(first.eventId, True)
        self.check_pixel_value(first.copyDestination, 4, 4, [1, 0, 0, 1])
        self.controller.SetFrameEvent(second.eventId, True)
        self.check_pixel_value(second.copyDestination, 4, 4, [0, 1, 0, 1])
        self.controller.SetFrameEvent(self.get_last_action().eventId, True)
        for name, color in [('ExternalWrite::FirstConsumer', [1, 0, 0, 1]),
                            ('ExternalWrite::SecondConsumer', [0, 1, 0, 1])]:
            resource = self.get_resource_by_name(name)
            self.check_pixel_value(resource.resourceId, 4, 4, color)
        half = self.get_resource_by_name('ExternalWrite::HalfFloat')
        self.check_pixel_value(half.resourceId, 4, 4, [0.25, 0.5, 0.75, 1])
        messages = self.controller.GetDebugMessages()
        self.check(any('private inference is not re-executed' in m.description for m in messages))
        usages = self.controller.GetUsage(self.get_resource_by_name('ExternalWrite::Input').resourceId)
        self.check(any(u.eventId == first.eventId and u.usage == rd.ResourceUsage.All_Resource for u in usages))
        self.check(any(u.eventId == second.eventId and u.usage == rd.ResourceUsage.All_Resource for u in usages))
        history = self.controller.PixelHistory(first.copyDestination, 4, 4, rd.Subresource(), rd.CompType.Typeless)
        self.check(any(m.eventId == first.eventId for m in history))
        self.check(any(m.eventId == second.eventId for m in history))
        self.check_export(self.capture_filename)
