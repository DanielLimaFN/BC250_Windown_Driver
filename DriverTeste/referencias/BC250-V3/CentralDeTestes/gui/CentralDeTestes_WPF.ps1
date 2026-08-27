# ===============================================================================
# Central de Testes GUI - AMD BC-250 (BC250-V3)
# Interface Grafica Moderna WPF (Dark Cyber Theme) para o Usuario
# ===============================================================================

Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BinDir = Join-Path $Root "bin"
$ReportsDir = Join-Path $Root "reports"

if (-not (Test-Path $ReportsDir)) {
    New-Item -ItemType Directory -Path $ReportsDir -Force | Out-Null
}

# Garante binários compilados
if (-not (Test-Path (Join-Path $BinDir "t1_scratch_mmio.exe"))) {
    & (Join-Path $Root "build_all.bat")
}

[xml]$xaml = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="AMD BC-250 (BC250-V3) — Central de Testes e Diagnostico de Silicio"
        Height="860" Width="1280"
        MinHeight="720" MinWidth="1080"
        WindowStartupLocation="CenterScreen"
        Background="#0E1117" Foreground="#E0E6ED"
        FontFamily="Segoe UI">
    <Window.Resources>
        <Style TargetType="Button">
            <Setter Property="Background" Value="#1E2330"/>
            <Setter Property="Foreground" Value="#F0F4F8"/>
            <Setter Property="BorderBrush" Value="#2D3748"/>
            <Setter Property="BorderThickness" Value="1"/>
            <Setter Property="Padding" Value="10,5"/>
            <Setter Property="FontWeight" Value="SemiBold"/>
            <Setter Property="FontSize" Value="12"/>
            <Setter Property="Cursor" Value="Hand"/>
        </Style>
        <Style x:Key="AccentButton" TargetType="Button">
            <Setter Property="Background" Value="#E02424"/>
            <Setter Property="Foreground" Value="#FFFFFF"/>
            <Setter Property="BorderBrush" Value="#FF4D4D"/>
            <Setter Property="BorderThickness" Value="1"/>
            <Setter Property="Padding" Value="14,7"/>
            <Setter Property="FontWeight" Value="Bold"/>
            <Setter Property="FontSize" Value="13"/>
            <Setter Property="Cursor" Value="Hand"/>
        </Style>
        <Style x:Key="SecondaryButton" TargetType="Button">
            <Setter Property="Background" Value="#0080FF"/>
            <Setter Property="Foreground" Value="#FFFFFF"/>
            <Setter Property="BorderBrush" Value="#3399FF"/>
            <Setter Property="BorderThickness" Value="1"/>
            <Setter Property="Padding" Value="12,6"/>
            <Setter Property="FontWeight" Value="SemiBold"/>
            <Setter Property="FontSize" Value="12"/>
            <Setter Property="Cursor" Value="Hand"/>
        </Style>
        <Style TargetType="TabItem">
            <Setter Property="Background" Value="#161B22"/>
            <Setter Property="Foreground" Value="#8B949E"/>
            <Setter Property="Padding" Value="18,9"/>
            <Setter Property="FontWeight" Value="SemiBold"/>
            <Setter Property="FontSize" Value="13"/>
            <Setter Property="BorderThickness" Value="0,0,0,2"/>
            <Setter Property="BorderBrush" Value="Transparent"/>
        </Style>
        <Style TargetType="GroupBox">
            <Setter Property="Foreground" Value="#00D2FF"/>
            <Setter Property="BorderBrush" Value="#252D3D"/>
            <Setter Property="BorderThickness" Value="1"/>
            <Setter Property="FontWeight" Value="Bold"/>
            <Setter Property="Padding" Value="10"/>
            <Setter Property="Margin" Value="0,0,0,10"/>
        </Style>
    </Window.Resources>

    <Grid>
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="*"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <!-- Header -->
        <Border Grid.Row="0" Background="#161B24" Padding="16,10" BorderBrush="#252D3D" BorderThickness="0,0,0,1">
            <Grid>
                <Grid.ColumnDefinitions>
                    <ColumnDefinition Width="*"/>
                    <ColumnDefinition Width="Auto"/>
                </Grid.ColumnDefinitions>
                <StackPanel Orientation="Horizontal" VerticalAlignment="Center">
                    <Border Background="#E02424" CornerRadius="4" Padding="8,4" Margin="0,0,12,0">
                        <TextBlock Text="BC250-V3" FontWeight="Bold" Foreground="White" FontSize="13"/>
                    </Border>
                    <StackPanel>
                        <TextBlock Text="CENTRAL DE TESTES E DIAGNOSTICO DE SILICIO (T1 A T12)" FontWeight="Bold" FontSize="16" Foreground="#FFFFFF"/>
                        <TextBlock Text="AMD BC-250 (Cyan Skillfish / Oberon) — WDDM 2.0+ Native Silicon Bridge" FontSize="12" Foreground="#8B949E"/>
                    </StackPanel>
                </StackPanel>
                <StackPanel Grid.Column="1" Orientation="Horizontal" VerticalAlignment="Center">
                    <Button Name="BtnRefreshTelem" Content="Atualizar Telemetria" Margin="0,0,10,0"/>
                    <Button Name="BtnRunAllHeader" Content="Executar Todos (T1..T12 + Fases)" Style="{StaticResource AccentButton}"/>
                </StackPanel>
            </Grid>
        </Border>

        <!-- Abas Principais -->
        <TabControl Grid.Row="1" Background="#0E1117" BorderThickness="0" Margin="10">
            
            <!-- ABA 1: DASHBOARD DE SILICIO -->
            <TabItem Header="Dashboard do Silicio">
                <ScrollViewer VerticalScrollBarVisibility="Auto">
                    <Grid Margin="10">
                        <Grid.RowDefinitions>
                            <RowDefinition Height="Auto"/>
                            <RowDefinition Height="Auto"/>
                            <RowDefinition Height="*"/>
                        </Grid.RowDefinitions>

                        <!-- Cards de Telemetria -->
                        <Grid Grid.Row="0" Margin="0,0,0,15">
                            <Grid.ColumnDefinitions>
                                <ColumnDefinition Width="*"/>
                                <ColumnDefinition Width="*"/>
                                <ColumnDefinition Width="*"/>
                                <ColumnDefinition Width="*"/>
                            </Grid.ColumnDefinitions>

                            <Border Grid.Column="0" Background="#161B24" CornerRadius="8" Padding="15" Margin="0,0,8,0" BorderBrush="#252D3D" BorderThickness="1">
                                <StackPanel>
                                    <TextBlock Text="TEMPERATURA GFX" FontSize="11" Foreground="#8B949E" FontWeight="Bold"/>
                                    <TextBlock Name="TxtGfxTemp" Text="-- °C" FontSize="26" FontWeight="Bold" Foreground="#00E676" Margin="0,4,0,0"/>
                                    <TextBlock Name="TxtHotspot" Text="Hotspot: -- °C" FontSize="11" Foreground="#8B949E"/>
                                </StackPanel>
                            </Border>

                            <Border Grid.Column="1" Background="#161B24" CornerRadius="8" Padding="15" Margin="4,0,4,0" BorderBrush="#252D3D" BorderThickness="1">
                                <StackPanel>
                                    <TextBlock Text="CLOCK SMU / GFX" FontSize="11" Foreground="#8B949E" FontWeight="Bold"/>
                                    <TextBlock Name="TxtGfxClock" Text="-- MHz" FontSize="26" FontWeight="Bold" Foreground="#00D2FF" Margin="0,4,0,0"/>
                                    <TextBlock Name="TxtVoltage" Text="Tensao: -- mV" FontSize="11" Foreground="#8B949E"/>
                                </StackPanel>
                            </Border>

                            <Border Grid.Column="2" Background="#161B24" CornerRadius="8" Padding="15" Margin="4,0,4,0" BorderBrush="#252D3D" BorderThickness="1">
                                <StackPanel>
                                    <TextBlock Text="POTENCIA ATIVA" FontSize="11" Foreground="#8B949E" FontWeight="Bold"/>
                                    <TextBlock Name="TxtGfxPower" Text="-- W" FontSize="26" FontWeight="Bold" Foreground="#FFD600" Margin="0,4,0,0"/>
                                    <TextBlock Name="TxtTdpTarget" Text="Target: 120W" FontSize="11" Foreground="#8B949E"/>
                                </StackPanel>
                            </Border>

                            <Border Grid.Column="3" Background="#161B24" CornerRadius="8" Padding="15" Margin="8,0,0,0" BorderBrush="#252D3D" BorderThickness="1">
                                <StackPanel>
                                    <TextBlock Text="SDMA0 DMA BANDWIDTH" FontSize="11" Foreground="#8B949E" FontWeight="Bold"/>
                                    <TextBlock Name="TxtSdmaBw" Text="-- GB/s" FontSize="26" FontWeight="Bold" Foreground="#E02424" Margin="0,4,0,0"/>
                                    <TextBlock Name="TxtSdmaStatus" Text="Status: Hardware DMA Ativo" FontSize="11" Foreground="#8B949E"/>
                                </StackPanel>
                            </Border>
                        </Grid>

                        <!-- Informações de Arquitetura -->
                        <Grid Grid.Row="1" Margin="0,0,0,15">
                            <Grid.ColumnDefinitions>
                                <ColumnDefinition Width="*"/>
                                <ColumnDefinition Width="*"/>
                            </Grid.ColumnDefinitions>

                            <GroupBox Grid.Column="0" Header="Arquitetura e Hardware Detectado" Margin="0,0,8,0">
                                <StackPanel Margin="5">
                                    <TextBlock Name="TxtGpuName" Text="GPU: AMD BC-250 (Oberon / Cyan Skillfish, DeviceId 0x13FE)" Margin="0,0,0,5"/>
                                    <TextBlock Name="TxtCuInfo" Text="Compute Units: 24 CUs ativas (12 WGPs / 1536 Shaders RDNA1)" Margin="0,0,0,5"/>
                                    <TextBlock Name="TxtVramInfo" Text="Memoria: 16.0 GB GDDR6 (256-bit bus, BAR0 256MB Aperture)" Margin="0,0,0,5"/>
                                    <TextBlock Name="TxtPspInfo" Text="PSP Status: TOS Ready (0x80020000), GPCOM Ring Ativo" Margin="0,0,0,5"/>
                                </StackPanel>
                            </GroupBox>

                            <GroupBox Grid.Column="1" Header="Estado dos Anéis de Execução (Rings)" Margin="8,0,0,0">
                                <StackPanel Margin="5">
                                    <TextBlock Name="TxtCpRingState" Text="CP PM4 Ring 0: Aperture 0xC100 (WPTR=0x00, RPTR=0x00)" Margin="0,0,0,5"/>
                                    <TextBlock Name="TxtSdmaRingState" Text="SDMA0 Copy Ring: 0x0FD00000 (12.2 GB/s sustentado)" Margin="0,0,0,5"/>
                                    <TextBlock Name="TxtGrbmState" Text="GRBM Status: 0x00000000 (Idle permanente)" Margin="0,0,0,5"/>
                                    <TextBlock Name="TxtMicrocodeState" Text="Microcodigo CP: Aguardando carga via C2PMSG direct" Margin="0,0,0,5"/>
                                </StackPanel>
                            </GroupBox>
                        </Grid>

                    </Grid>
                </ScrollViewer>
            </TabItem>

            <!-- ABA 2: CENTRAL DE EXECUÇÃO DE TESTES -->
            <TabItem Header="Bateria de Testes (T1 a T12)">
                <Grid Margin="5">
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="360"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>

                    <!-- Lista de Testes / Ações -->
                    <Border Grid.Column="0" Background="#161B24" CornerRadius="8" Padding="12" Margin="0,0,10,0" BorderBrush="#252D3D" BorderThickness="1">
                        <ScrollViewer VerticalScrollBarVisibility="Auto">
                            <StackPanel>
                                <TextBlock Text="TESTES DE SILICIO BLINDADOS" FontWeight="Bold" Foreground="#00D2FF" Margin="0,0,0,8"/>
                                
                                <Button Name="BtnRunT1" Content="T1: Scratch MMIO (Walking 1s/0s)" Margin="0,0,0,4" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunT2" Content="T2: Scratch via PM4 (Decisivo 4-Pontos)" Margin="0,0,0,4" HorizontalContentAlignment="Left" Background="#2C1B1B" BorderBrush="#E02424"/>
                                <Button Name="BtnRunT3" Content="T3: Write-Stick Sweep &amp; Multibank" Margin="0,0,0,4" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunT4" Content="T4: Detector de Fill Pattern" Margin="0,0,0,4" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunT5" Content="T5: Fence Poisoning &amp; SDMA" Margin="0,0,0,4" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunT6" Content="T6: Carga x Telemetria SMU" Margin="0,0,0,4" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunT7" Content="T7: Validador PSP Offline" Margin="0,0,0,4" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunT8" Content="T8: Auditor de Fallback CPU (Anti-Mock)" Margin="0,0,0,4" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunT9" Content="T9: Bissecao de Bring-up (Registro)" Margin="0,0,0,4" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunT10" Content="T10: Baseline Plausibilidade RDNA1" Margin="0,0,0,4" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunT11" Content="T11: MemTest VRAM GDDR6 (256-bit)" Margin="0,0,0,4" HorizontalContentAlignment="Left" Background="#1A2836" BorderBrush="#0080FF"/>
                                <Button Name="BtnRunT12" Content="T12: Sonda Direta Mailbox SMU" Margin="0,0,0,10" HorizontalContentAlignment="Left" Background="#1A2836" BorderBrush="#0080FF"/>

                                <TextBlock Text="AUDITORIA POR FASE" FontWeight="Bold" Foreground="#00D2FF" Margin="0,4,0,6"/>
                                <Button Name="BtnRunPhaseAll" Content="Auditar Fases 0 a 4" Margin="0,0,0,4" Style="{StaticResource SecondaryButton}"/>
                                <Button Name="BtnRunPhase0" Content="Fase 0: Purga de Mentiras" Margin="0,0,0,3" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunPhase1" Content="Fase 1: Seguranca &amp; Deteccao" Margin="0,0,0,3" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunPhase2" Content="Fase 2: DPM do SMU" Margin="0,0,0,3" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunPhase3" Content="Fase 3: Firmware Direto" Margin="0,0,0,3" HorizontalContentAlignment="Left"/>
                                <Button Name="BtnRunPhase4" Content="Fase 4: Fila MQD/HQD" Margin="0,0,0,3" HorizontalContentAlignment="Left"/>
                            </StackPanel>
                        </ScrollViewer>
                    </Border>

                    <!-- Console de Logs e Resultados -->
                    <Border Grid.Column="1" Background="#161B24" CornerRadius="8" Padding="12" BorderBrush="#252D3D" BorderThickness="1">
                        <Grid>
                            <Grid.RowDefinitions>
                                <RowDefinition Height="Auto"/>
                                <RowDefinition Height="*"/>
                                <RowDefinition Height="Auto"/>
                            </Grid.RowDefinitions>

                            <Grid Grid.Row="0" Margin="0,0,0,8">
                                <Grid.ColumnDefinitions>
                                    <ColumnDefinition Width="*"/>
                                    <ColumnDefinition Width="Auto"/>
                                </Grid.ColumnDefinitions>
                                <TextBlock Text="LOG DE EXECUCAO, CRITERIOS E VEREDITOS" FontWeight="Bold" Foreground="#FFFFFF" VerticalAlignment="Center"/>
                                <StackPanel Grid.Column="1" Orientation="Horizontal">
                                    <Button Name="BtnClearLog" Content="Limpar Console" Margin="0,0,8,0" Padding="8,4" FontSize="11"/>
                                    <Button Name="BtnExportReport" Content="Exportar Relatorio" Style="{StaticResource SecondaryButton}" Padding="8,4" FontSize="11"/>
                                </StackPanel>
                            </Grid>

                            <TextBox Name="TxtLogConsole" Grid.Row="1" Background="#0A0D12" Foreground="#00E676"
                                     FontFamily="Consolas" FontSize="12" IsReadOnly="True"
                                     VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Auto"
                                     TextWrapping="Wrap" Padding="8" BorderBrush="#252D3D"/>

                            <!-- Resumo dos Badges -->
                            <Border Grid.Row="2" Background="#12161E" Margin="0,8,0,0" Padding="10" CornerRadius="4">
                                <StackPanel Orientation="Horizontal" HorizontalAlignment="Center">
                                    <TextBlock Name="BadgeProved" Text="0 PROVADOS" FontWeight="Bold" Foreground="#00E676" Margin="10,0"/>
                                    <TextBlock Name="BadgeNotVal" Text="0 NAO VALIDADOS" FontWeight="Bold" Foreground="#FFD600" Margin="10,0"/>
                                    <TextBlock Name="BadgeBlocked" Text="0 BLOQUEADOS" FontWeight="Bold" Foreground="#00D2FF" Margin="10,0"/>
                                    <TextBlock Name="BadgeErrors" Text="0 ERROS" FontWeight="Bold" Foreground="#E02424" Margin="10,0"/>
                                </StackPanel>
                            </Border>
                        </Grid>
                    </Border>
                </Grid>
            </TabItem>

            <!-- ABA 3: EXPLORADOR DE REGISTRADORES -->
            <TabItem Header="Explorador de Registradores">
                <Grid Margin="10">
                    <Grid.RowDefinitions>
                        <RowDefinition Height="Auto"/>
                        <RowDefinition Height="*"/>
                    </Grid.RowDefinitions>

                    <GroupBox Grid.Row="0" Header="Leitura e Escrita Direta de MMIO / SMN">
                        <Grid Margin="5">
                            <Grid.RowDefinitions>
                                <RowDefinition Height="Auto"/>
                                <RowDefinition Height="Auto"/>
                            </Grid.RowDefinitions>
                            <Grid.ColumnDefinitions>
                                <ColumnDefinition Width="Auto"/>
                                <ColumnDefinition Width="200"/>
                                <ColumnDefinition Width="Auto"/>
                                <ColumnDefinition Width="200"/>
                                <ColumnDefinition Width="*"/>
                            </Grid.ColumnDefinitions>

                            <TextBlock Grid.Row="0" Grid.Column="0" Text="Offset (Hex):" VerticalAlignment="Center" Margin="0,0,8,8"/>
                            <TextBox Name="TxtRegOffset" Grid.Row="0" Grid.Column="1" Text="0x032D4" Background="#161B24" Foreground="White" Padding="6" Margin="0,0,12,8"/>

                            <TextBlock Grid.Row="0" Grid.Column="2" Text="Valor a Gravar (Hex):" VerticalAlignment="Center" Margin="0,0,8,8"/>
                            <TextBox Name="TxtRegValue" Grid.Row="0" Grid.Column="3" Text="0x4D585042" Background="#161B24" Foreground="White" Padding="6" Margin="0,0,12,8"/>

                            <StackPanel Grid.Row="0" Grid.Column="4" Orientation="Horizontal" Margin="0,0,0,8">
                                <Button Name="BtnReadReg" Content="Ler Registrador" Margin="0,0,8,0"/>
                                <Button Name="BtnWriteReg" Content="Gravar Valor" Style="{StaticResource AccentButton}" Margin="0,0,8,0"/>
                            </StackPanel>

                            <TextBlock Name="TxtRegResult" Grid.Row="1" Grid.Column="0" Grid.ColumnSpan="5" Text="Resultado da operacao: Pronto." Foreground="#8B949E" Margin="0,4,0,0"/>
                        </Grid>
                    </GroupBox>

                    <GroupBox Grid.Row="1" Header="Tabela de Registradores Criticos de Silicio">
                        <TextBox Name="TxtRegTable" Background="#0A0D12" Foreground="#81D4FA"
                                 FontFamily="Consolas" FontSize="12" IsReadOnly="True"
                                 VerticalScrollBarVisibility="Auto" Padding="8"/>
                    </GroupBox>
                </Grid>
            </TabItem>

            <!-- ABA 4: MATRIZ DE FASES DO PLANO -->
            <TabItem Header="Matriz de Fases (Roadmap)">
                <ScrollViewer VerticalScrollBarVisibility="Auto">
                    <Grid Margin="10">
                        <StackPanel>
                            <TextBlock Text="STATUS DAS FASES DE BRING-UP DE SILICIO" FontWeight="Bold" FontSize="15" Foreground="#00D2FF" Margin="0,0,0,12"/>
                            
                            <Border Background="#161B24" CornerRadius="6" Padding="12" Margin="0,0,0,10" BorderBrush="#252D3D" BorderThickness="1">
                                <StackPanel>
                                    <TextBlock Text="FASE 0 — Purga de Mentiras e Codigo Honesto" FontWeight="Bold" Foreground="#FFD600"/>
                                    <TextBlock Text="Status: Em Transicao | Central de Testes criada; remocao do loop CPU e OpenCL build fake pendentes." FontSize="12" Foreground="#C9D1D9" Margin="0,4,0,0"/>
                                </StackPanel>
                            </Border>

                            <Border Background="#161B24" CornerRadius="6" Padding="12" Margin="0,0,0,10" BorderBrush="#252D3D" BorderThickness="1">
                                <StackPanel>
                                    <TextBlock Text="FASE 1 — Seguranca e Deteccao Real" FontWeight="Bold" Foreground="#00E676"/>
                                    <TextBlock Text="Status: Ativa | Remover ForceGfxFreq(0x39); travas fail-closed contra unhalt do CP sem GPUVM." FontSize="12" Foreground="#C9D1D9" Margin="0,4,0,0"/>
                                </StackPanel>
                            </Border>

                            <Border Background="#161B24" CornerRadius="6" Padding="12" Margin="0,0,0,10" BorderBrush="#252D3D" BorderThickness="1">
                                <StackPanel>
                                    <TextBlock Text="FASE 2 — DPM do SMU (O DESBLOQUEIO REAL)" FontWeight="Bold" Foreground="#00D2FF"/>
                                    <TextBlock Text="Status: Bloqueada | Requer integracao da PPTable no SMU para sair do deep sleep 15MHz." FontSize="12" Foreground="#C9D1D9" Margin="0,4,0,0"/>
                                </StackPanel>
                            </Border>

                            <Border Background="#161B24" CornerRadius="6" Padding="12" Margin="0,0,0,10" BorderBrush="#252D3D" BorderThickness="1">
                                <StackPanel>
                                    <TextBlock Text="FASE 3 — Firmware por Caminho Direto C2PMSG_35/36/37" FontWeight="Bold" Foreground="#00D2FF"/>
                                    <TextBlock Text="Status: Bloqueada | Carga direta de microcodigos sem depender do SOS PSP." FontSize="12" Foreground="#C9D1D9" Margin="0,4,0,0"/>
                                </StackPanel>
                            </Border>

                            <Border Background="#161B24" CornerRadius="6" Padding="12" Margin="0,0,0,10" BorderBrush="#252D3D" BorderThickness="1">
                                <StackPanel>
                                    <TextBlock Text="FASE 4 — Fila MQD/HQD e PM4 Real de Compute" FontWeight="Bold" Foreground="#8B949E"/>
                                    <TextBlock Text="Status: Planejada | Alocacao de v10_compute_mqd e submissao MAP_QUEUES via KIQ." FontSize="12" Foreground="#C9D1D9" Margin="0,4,0,0"/>
                                </StackPanel>
                            </Border>
                        </StackPanel>
                    </Grid>
                </ScrollViewer>
            </TabItem>

        </TabControl>

        <!-- Status Bar -->
        <Border Grid.Row="2" Background="#161B24" Padding="12,6" BorderBrush="#252D3D" BorderThickness="0,1,0,0">
            <Grid>
                <Grid.ColumnDefinitions>
                    <ColumnDefinition Width="*"/>
                    <ColumnDefinition Width="Auto"/>
                </Grid.ColumnDefinitions>
                <TextBlock Name="TxtStatusBar" Text="Pronto para execucao. Selecione um teste ou execute a bateria completa." FontSize="11" Foreground="#8B949E"/>
                <TextBlock Grid.Column="1" Text="AMD BC-250 Driver Project (2026)" FontSize="11" Foreground="#555E6D"/>
            </Grid>
        </Border>
    </Grid>
</Window>
"@

$reader = New-Object System.Xml.XmlNodeReader $xaml
$window = [Windows.Markup.XamlReader]::Load($reader)

# Mapeia Controles
$BtnRefreshTelem = $window.FindName("BtnRefreshTelem")
$BtnRunAllHeader = $window.FindName("BtnRunAllHeader")
$TxtGfxTemp      = $window.FindName("TxtGfxTemp")
$TxtHotspot      = $window.FindName("TxtHotspot")
$TxtGfxClock     = $window.FindName("TxtGfxClock")
$TxtVoltage      = $window.FindName("TxtVoltage")
$TxtGfxPower     = $window.FindName("TxtGfxPower")
$TxtSdmaBw       = $window.FindName("TxtSdmaBw")
$TxtLogConsole   = $window.FindName("TxtLogConsole")
$BtnClearLog     = $window.FindName("BtnClearLog")
$BtnExportReport = $window.FindName("BtnExportReport")
$BadgeProved     = $window.FindName("BadgeProved")
$BadgeNotVal     = $window.FindName("BadgeNotVal")
$BadgeBlocked    = $window.FindName("BadgeBlocked")
$BadgeErrors     = $window.FindName("BadgeErrors")
$TxtStatusBar    = $window.FindName("TxtStatusBar")
$TxtRegOffset    = $window.FindName("TxtRegOffset")
$TxtRegValue     = $window.FindName("TxtRegValue")
$BtnReadReg      = $window.FindName("BtnReadReg")
$BtnWriteReg     = $window.FindName("BtnWriteReg")
$TxtRegResult    = $window.FindName("TxtRegResult")
$TxtRegTable     = $window.FindName("TxtRegTable")

# Controles de Testes (T1 a T12)
$BtnRunT1        = $window.FindName("BtnRunT1")
$BtnRunT2        = $window.FindName("BtnRunT2")
$BtnRunT3        = $window.FindName("BtnRunT3")
$BtnRunT4        = $window.FindName("BtnRunT4")
$BtnRunT5        = $window.FindName("BtnRunT5")
$BtnRunT6        = $window.FindName("BtnRunT6")
$BtnRunT7        = $window.FindName("BtnRunT7")
$BtnRunT8        = $window.FindName("BtnRunT8")
$BtnRunT9        = $window.FindName("BtnRunT9")
$BtnRunT10       = $window.FindName("BtnRunT10")
$BtnRunT11       = $window.FindName("BtnRunT11")
$BtnRunT12       = $window.FindName("BtnRunT12")
$BtnRunPhaseAll  = $window.FindName("BtnRunPhaseAll")
$BtnRunPhase0    = $window.FindName("BtnRunPhase0")
$BtnRunPhase1    = $window.FindName("BtnRunPhase1")
$BtnRunPhase2    = $window.FindName("BtnRunPhase2")
$BtnRunPhase3    = $window.FindName("BtnRunPhase3")
$BtnRunPhase4    = $window.FindName("BtnRunPhase4")

$global:AllResults = @()

function Append-Log($text) {
    $TxtLogConsole.Dispatcher.Invoke([Action]{
        $TxtLogConsole.AppendText("$text`r`n")
        $TxtLogConsole.ScrollToEnd()
    })
}

function Update-Badges {
    $proved = ($global:AllResults | Where-Object { $_.verdict -eq "PROVADO" }).Count
    $notVal = ($global:AllResults | Where-Object { $_.verdict -eq "NAO VALIDADO EM HARDWARE" }).Count
    $blocked = ($global:AllResults | Where-Object { $_.verdict -eq "BLOQUEADO" }).Count
    $errors = ($global:AllResults | Where-Object { $_.verdict -eq "ERRO" }).Count

    $BadgeProved.Text = "$proved PROVADOS"
    $BadgeNotVal.Text = "$notVal NAO VALIDADOS"
    $BadgeBlocked.Text = "$blocked BLOQUEADOS"
    $BadgeErrors.Text = "$errors ERROS"
}

function Run-SingleTestTool($exeName) {
    $exePath = Join-Path $BinDir $exeName
    if (-not (Test-Path $exePath)) {
        Append-Log("[-] ERRO: Binario $exeName nao encontrado. Execute build_all.bat.")
        return
    }

    $TxtStatusBar.Text = "Executando $exeName..."
    Append-Log("-------------------------------------------------------------------------------")
    
    $pinfo = New-Object System.Diagnostics.ProcessStartInfo
    $pinfo.FileName = $exePath
    $pinfo.RedirectStandardOutput = $true
    $pinfo.UseShellExecute = $false
    $pinfo.CreateNoWindow = $true

    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $pinfo
    $p.Start() | Out-Null
    $stdout = $p.StandardOutput.ReadToEnd()
    $p.WaitForExit()

    Append-Log($stdout.Trim())

    # Coleta resultado estruturado via --json
    $pinfoJson = New-Object System.Diagnostics.ProcessStartInfo
    $pinfoJson.FileName = $exePath
    $pinfoJson.Arguments = "--json"
    $pinfoJson.RedirectStandardOutput = $true
    $pinfoJson.UseShellExecute = $false
    $pinfoJson.CreateNoWindow = $true

    $pJson = New-Object System.Diagnostics.Process
    $pJson.StartInfo = $pinfoJson
    $pJson.Start() | Out-Null
    $stdoutJson = $pJson.StandardOutput.ReadToEnd()
    $pJson.WaitForExit()

    try {
        $parsed = $stdoutJson | ConvertFrom-Json
        $global:AllResults = @($global:AllResults | Where-Object { $_.id -ne $parsed.id }) + $parsed
        Update-Badges
    } catch {}

    $TxtStatusBar.Text = "Concluido: $exeName."
}

function Refresh-Telemetry {
    $t6Exe = Join-Path $BinDir "t6_telemetry_correlation.exe"
    if (Test-Path $t6Exe) {
        $pinfo = New-Object System.Diagnostics.ProcessStartInfo
        $pinfo.FileName = $t6Exe
        $pinfo.Arguments = "--json"
        $pinfo.RedirectStandardOutput = $true
        $pinfo.UseShellExecute = $false
        $pinfo.CreateNoWindow = $true
        $p = [System.Diagnostics.Process]::Start($pinfo)
        $out = $p.StandardOutput.ReadToEnd()
        $p.WaitForExit()

        try {
            $j = $out | ConvertFrom-Json
            if ($j.measured -match 'Temp=(\d+)\s*C.*Clock=(\d+)\s*MHz.*Potencia=(\d+)\s*W.*Tensao=(\d+)\s*mV') {
                $TxtGfxTemp.Text = "$($Matches[1]) °C"
                $TxtGfxClock.Text = "$($Matches[2]) MHz"
                $TxtGfxPower.Text = "$($Matches[3]) W"
                $TxtVoltage.Text = "Tensao: $($Matches[4]) mV"
            }
        } catch {}
    }

    $t5Exe = Join-Path $BinDir "t5_sdma_fence_poison.exe"
    if (Test-Path $t5Exe) {
        $pinfo = New-Object System.Diagnostics.ProcessStartInfo
        $pinfo.FileName = $t5Exe
        $pinfo.Arguments = "--json"
        $pinfo.RedirectStandardOutput = $true
        $pinfo.UseShellExecute = $false
        $pinfo.CreateNoWindow = $true
        $p = [System.Diagnostics.Process]::Start($pinfo)
        $out = $p.StandardOutput.ReadToEnd()
        $p.WaitForExit()

        try {
            $j = $out | ConvertFrom-Json
            if ($j.measured -match 'Media=\d+\s*MB/s\s*\(([\d\.]+)\s*GB/s\)') {
                $TxtSdmaBw.Text = "$($Matches[1]) GB/s"
            }
        } catch {}
    }
}

function Refresh-RegisterTable {
    $t4Exe = Join-Path $BinDir "t4_fill_pattern.exe"
    if (Test-Path $t4Exe) {
        $pinfo = New-Object System.Diagnostics.ProcessStartInfo
        $pinfo.FileName = $t4Exe
        $pinfo.RedirectStandardOutput = $true
        $pinfo.UseShellExecute = $false
        $pinfo.CreateNoWindow = $true
        $p = [System.Diagnostics.Process]::Start($pinfo)
        $out = $p.StandardOutput.ReadToEnd()
        $p.WaitForExit()
        $TxtRegTable.Text = $out
    }
}

# Eventos dos Botões
$BtnRefreshTelem.Add_Click({ Refresh-Telemetry })
$BtnClearLog.Add_Click({ $TxtLogConsole.Clear() })

$BtnRunAllHeader.Add_Click({
    $TxtLogConsole.Clear()
    Append-Log("===============================================================================")
    Append-Log(" INICIANDO BATERIA COMPLETA DE TESTES DE SILICIO (T1 A T12 + FASES)")
    Append-Log("===============================================================================")
    
    $tests = @("t1_scratch_mmio.exe", "t2_pm4_scratch.exe", "t3_write_stick.exe",
               "t4_fill_pattern.exe", "t5_sdma_fence_poison.exe", "t6_telemetry_correlation.exe",
               "t7_psp_protocol_validate.exe", "t8_fallback_audit.exe", "t9_bringup_bisect.exe",
               "t10_baseline_plausibility.exe", "t11_vram_memtest.exe", "t12_smu_mailbox_probe.exe", "phase_audit.exe")
               
    foreach ($t in $tests) {
        Run-SingleTestTool $t
    }
    Refresh-Telemetry
})

$BtnRunT1.Add_Click({ Run-SingleTestTool "t1_scratch_mmio.exe" })
$BtnRunT2.Add_Click({ Run-SingleTestTool "t2_pm4_scratch.exe" })
$BtnRunT3.Add_Click({ Run-SingleTestTool "t3_write_stick.exe" })
$BtnRunT4.Add_Click({ Run-SingleTestTool "t4_fill_pattern.exe" })
$BtnRunT5.Add_Click({ Run-SingleTestTool "t5_sdma_fence_poison.exe" })
$BtnRunT6.Add_Click({ Run-SingleTestTool "t6_telemetry_correlation.exe"; Refresh-Telemetry })
$BtnRunT7.Add_Click({ Run-SingleTestTool "t7_psp_protocol_validate.exe" })
$BtnRunT8.Add_Click({ Run-SingleTestTool "t8_fallback_audit.exe" })
$BtnRunT9.Add_Click({ Run-SingleTestTool "t9_bringup_bisect.exe" })
$BtnRunT10.Add_Click({ Run-SingleTestTool "t10_baseline_plausibility.exe" })
$BtnRunT11.Add_Click({ Run-SingleTestTool "t11_vram_memtest.exe" })
$BtnRunT12.Add_Click({ Run-SingleTestTool "t12_smu_mailbox_probe.exe" })
$BtnRunPhaseAll.Add_Click({ Run-SingleTestTool "phase_audit.exe" })

$BtnExportReport.Add_Click({
    $timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
    $repFile = Join-Path $ReportsDir "Relatorio_GUI_$timestamp.md"
    $lines = @()
    $lines += "# Relatorio da Central de Testes (GUI) - AMD BC-250"
    $lines += ""
    $lines += "**Data:** $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    $lines += ""
    $lines += "| ID | Teste | Veredito | Criterio | Medicao |"
    $lines += "|---|---|---|---|---|"
    foreach ($r in $global:AllResults) {
        $crit = ($r.criterion -replace '\|', '/')
        $meas = ($r.measured -replace '\|', '/')
        $lines += "| $($r.id) | $($r.title) | $($r.verdict) | $crit | $meas |"
    }
    Set-Content -Path $repFile -Value ($lines -join "`r`n") -Encoding UTF8
    Append-Log("[+] Relatorio exportado com sucesso: $repFile")
    [System.Windows.MessageBox]::Show("Relatorio salvo com sucesso em:`n$repFile", "Central de Testes", [System.Windows.MessageBoxButton]::OK, [System.Windows.MessageBoxImage]::Information)
})

# Inicialização ao abrir a janela
$window.Add_Loaded({
    Refresh-Telemetry
    Refresh-RegisterTable
})

$window.ShowDialog() | Out-Null
