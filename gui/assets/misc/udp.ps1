    [cmdletbinding(
            DefaultParameterSetName = '',
            ConfirmImpact = 'low'
    )]
    Param(
        [Parameter(
                Mandatory = $True,
                Position = 0,
                ParameterSetName = '',
                ValueFromPipeline = $True)]
        [String]$computer,
        [Parameter(
                Position = 1,
                Mandatory = $True,
                ParameterSetName = '')]
        [Int16]$port
    )
    Process {
        $udpobject = new-Object system.Net.Sockets.Udpclient
        $udpobject.client.ReceiveTimeout = 2000
        $udpobject.Connect("$computer", $port)
        $a = new-object system.text.asciiencoding
        $byte = $a.GetBytes("$( Get-Date )")
        [void]$udpobject.Send($byte, $byte.length)
        $remoteendpoint = New-Object system.net.ipendpoint([system.net.ipaddress]::Any, 0)
        Try
        {
            $receivebytes = $udpobject.Receive([ref]$remoteendpoint)
            [string]$returndata = $a.GetString($receivebytes)
            If ($returndata)
            {
                exit 0
            }
        }
        Catch
        {
            $udpobject.close()
            exit 1
        }
    }
